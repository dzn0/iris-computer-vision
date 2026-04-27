#define NOMINMAX
#include "../hdr/MouseController.h"
#include <windows.h>
#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>


MouseController::MouseController(int screen_width, int screen_height,
                                 int roi_x, int roi_y, int roi_w, int roi_h,
                                 float fov_h, float fov_v,
                                 float sensitivity, int dpi)
    : screen_width(screen_width), screen_height(screen_height),
      roi_x(roi_x), roi_y(roi_y), roi_w(roi_w), roi_h(roi_h),
      fov_horizontal(fov_h), fov_vertical(fov_v),
      sensitivity(sensitivity), dpi(dpi),
      damping(0.85f), deadzone_angle(3.0f), max_speed(500.0f) {
    // Fator de conversão reduzido para evitar overshooting
    // Começa em valor baixo, aumenta conforme necessário
    pixels_per_degree = 3.0f * sensitivity * (dpi / 800.0f);

    // Validação de diretório e pré-criação do arquivo
    CreateDirectoryA("C:\\Windows\\Temp\\", NULL);  // Idempotent - cria se não existir
    FILE* f = nullptr;
    errno_t err = fopen_s(&f, CMD_FILE, "w");
    if (f) fclose(f);  // Cria arquivo vazio para pré-validar caminho
}

MouseController::~MouseController() {
}

void MouseController::SetDamping(float d) {
    damping = std::max(0.0f, std::min(d, 1.0f));
}

void MouseController::SetDeadzone(float dz) {
    deadzone_angle = std::max(0.0f, dz);
}

void MouseController::SetMaxSpeed(float ms) {
    max_speed = std::max(1.0f, ms);
}

void MouseController::SetOvershooting(int val) {
    val = std::max(10, std::min(val, 200));
    // Normaliza para 1920x1080 como referência — em resoluções maiores
    // o FOV da ROI é proporcionalmente menor, então aumentamos pixels_per_degree
    float res_scale = (float)screen_width / 1920.0f;
    pixels_per_degree = (val / 10.0f) * sensitivity * (dpi / 800.0f) * res_scale;
}

bool MouseController::WriteCommand(int16_t x, int16_t y, uint8_t mode, uint8_t btn) {
    DeleteFileA(CMD_FILE);
    FILE* f = nullptr;
    if (fopen_s(&f, CMD_FILE, "w") != 0 || !f) return false;
    fprintf(f, "%d %d %d %d\n", (int)x, (int)y, (int)mode, (int)btn);
    fclose(f);
    return true;
}

bool MouseController::SendClick() {
    return WriteCommand(0, 0, 0, 1);
}

bool MouseController::WaitForFileDeletion(int timeout_ms) {
    const int MAX_BACKOFF_ATTEMPTS = 3;
    int current_timeout = timeout_ms;

    // Retry loop com exponential backoff: 5ms → 20ms → 80ms
    for (int attempt = 0; attempt < MAX_BACKOFF_ATTEMPTS; attempt++) {
        auto start = std::chrono::high_resolution_clock::now();

        while (true) {
            WIN32_FIND_DATAA find_data;
            HANDLE find_handle = FindFirstFileA(CMD_FILE, &find_data);

            if (find_handle == INVALID_HANDLE_VALUE) {
                // Arquivo não existe = ACK recebido do driver ✓
                return true;
            }

            FindClose(find_handle);

            // Timeout desta tentativa?
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start).count();
            if (elapsed >= current_timeout) {
                break;  // Timeout nesta tentativa, tenta próxima com timeout maior
            }

            // Aguarda 1ms antes de próximo poll
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Exponential backoff: multiplica timeout por 4 para próxima tentativa
        current_timeout *= 4;
    }

    // Todas as tentativas esgotadas
    return false;
}

bool MouseController::MoveToTarget(float center_x, float center_y) {
    // ── Delta-time ────────────────────────────────────────────────────────────
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - last_move_time).count();
    if (last_move_time.time_since_epoch().count() == 0 || dt > 0.1f)
        dt = 1.0f / 120.0f;   // clamp primeiro frame ou pausa longa
    last_move_time = now;

    // ── Offset do centro (normalizado -1..1) ─────────────────────────────────
    float dx = (center_x - 0.5f) * 2.0f;
    float dy = (center_y - 0.5f) * 2.0f;

    // ── Deadzone angular ─────────────────────────────────────────────────────
    // FOV da ROI — a detecção é normalizada dentro da ROI, não da tela inteira
    float roi_fov_h = fov_horizontal * ((float)roi_w / (float)screen_width);
    float roi_fov_v = fov_vertical   * ((float)roi_h / (float)screen_height);
    float angle_x = dx * (roi_fov_h / 2.0f);
    float angle_y = dy * (roi_fov_v / 2.0f);
    float dist_deg = std::sqrt(angle_x * angle_x + angle_y * angle_y);
    if (deadzone_angle > 0.0f && dist_deg < deadzone_angle) {
        // Decai suavemente em vez de zerar abruptamente — evita salto ao sair da deadzone
        smooth_x *= 0.8f; smooth_y *= 0.8f;
        frac_x   *= 0.8f; frac_y   *= 0.8f;
        return true;
    }

    // ── Converter para pixels ─────────────────────────────────────────────────
    float raw_x = angle_x * pixels_per_degree;
    float raw_y = angle_y * pixels_per_degree;

    // ── EMA smoothing com delta-time ──────────────────────────────────────────
    // alpha normalizado para 120fps — mantém comportamento consistente em qualquer FPS
    float alpha = (damping <= 0.0f) ? 1.0f
                : std::min(1.0f, (1.0f - damping) * (dt * 120.0f));

    smooth_x += alpha * (raw_x - smooth_x);
    smooth_y += alpha * (raw_y - smooth_y);

    // ── Velocity clamping (só aplica com smoothing ativo) ────────────────────
    float sx = smooth_x, sy = smooth_y;
    if (damping > 0.0f) {
        float velocity = std::sqrt(sx * sx + sy * sy);
        if (velocity > max_speed) {
            float scale = max_speed / velocity;
            sx *= scale; sy *= scale;
        }
    }

    // ── Sub-pixel accumulation (evita truncar movimentos pequenos) ────────────
    frac_x += sx;
    frac_y += sy;
    int16_t ix = static_cast<int16_t>(frac_x);
    int16_t iy = static_cast<int16_t>(frac_y);
    frac_x -= ix;
    frac_y -= iy;

    if (ix == 0 && iy == 0) return true;  // nada a enviar

    // ── Fire-and-forget: não bloqueia esperando ACK do driver ────────────────
    return WriteCommand(ix, iy, 0, 0);
}
