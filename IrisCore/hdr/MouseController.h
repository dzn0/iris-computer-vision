#pragma once
#include <string>
#include <cstdint>
#include <chrono>

class MouseController {
public:
    MouseController(int screen_width = 2560, int screen_height = 1440,
                    int roi_x = 960, int roi_y = 480, int roi_w = 640, int roi_h = 640,
                    float fov_h = 103.0f, float fov_v = 70.0f,
                    float sensitivity = 1.0f, int dpi = 800);
    ~MouseController();

    // Move relativo para Valorant/jogos (delta em pixels)
    // center_x, center_y: posição normalizada do target (0.0-1.0)
    bool MoveToTarget(float center_x, float center_y);

    // Dispara um clique do botão esquerdo via driver
    bool SendClick();

    // Verifica se o arquivo de comando foi processado
    bool WaitForAck(int timeout_ms = 100);

    // Configurações de controle
    void SetDamping(float damping);      // 0.0-1.0 (0=nenhuma suavização, 1=muito lento)
    void SetDeadzone(float deadzone);    // Minimo ângulo antes de mover (graus)
    void SetMaxSpeed(float max_px_s);   // Limite de velocidade em pixels/segundo
    void SetOvershooting(int val);       // pixels_per_degree factor * 10 (10-200)
    void SetROI(int x, int y, int w, int h) { roi_x=x; roi_y=y; roi_w=w; roi_h=h; }

private:
    int screen_width;
    int screen_height;
    int roi_x;
    int roi_y;
    int roi_w;
    int roi_h;

    // Parâmetros de conversão para Valorant/jogos
    float fov_horizontal;    // FOV horizontal em graus (103°)
    float fov_vertical;      // FOV vertical em graus (70°)
    float sensitivity;       // Sensibilidade do jogo (1.0 = padrão)
    int dpi;                 // DPI do mouse
    float pixels_per_degree; // Fator de conversão calibrado

    // Controle de movimento
    float damping;           // Suavização exponencial (0.0=off, 1.0=muito lento)
    float deadzone_angle;    // Deadzone em graus
    float max_speed;         // Velocidade máxima em pixels/segundo

    // Estado de smoothing (EMA com delta-time)
    float smooth_x = 0.0f;
    float smooth_y = 0.0f;

    // Acumulador de sub-pixels (evita truncation de movimentos pequenos)
    float frac_x = 0.0f;
    float frac_y = 0.0f;

    // Delta-time para smoothing consistente independente do FPS
    std::chrono::high_resolution_clock::time_point last_move_time{};

    static constexpr const char* CMD_FILE = "C:\\Windows\\Temp\\wmi_ipc.dat";

    bool WriteCommand(int16_t x, int16_t y, uint8_t mode, uint8_t btn = 0);
    bool WaitForFileDeletion(int timeout_ms);
};
