#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cstdio>

#include "AntiDebug.h"
#include "Config.h"
#include "Overlay.h"
#include "DXGICaptureOptimized.h"
#include "YOLOInference.h"
#include "MouseController.h"
#include "resource.h"

#define HEARTBEAT_FILE  L"C:\\Windows\\Temp\\wuauclt.dat"
#define DLL_DIR         L"C:\\Windows\\Temp\\WinDiag\\"

// Detecta se o processo do Valorant está rodando
static bool IsValorantRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0) {
                found = true; break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

#define C_RST  "\033[0m"
#define C_INFO "\033[38;5;153m"
#define C_OK   "\033[38;5;147m"
#define C_ERR  "\033[38;5;210m"

static std::atomic<bool> g_running(true);
static std::atomic<bool> g_aiming(true);

// ── F1 toggle via GetAsyncKeyState (sem hook global visível ao Vanguard) ──────
// Detecta borda de descida (keydown) sem SetWindowsHookEx
static bool s_f1_prev = false;

static bool PollF1Toggle() {
    bool cur = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    bool toggled = cur && !s_f1_prev;
    s_f1_prev = cur;
    return toggled;
}

// ── DLL table ─────────────────────────────────────────────────────────────────
struct EmbeddedDLL { int id; const wchar_t* name; };
static const EmbeddedDLL g_dlls[] = {
    { IDR_DLL_DIRECTML,             L"DirectML.dll"                    },
    { IDR_DLL_ABSEIL,               L"abseil_dll.dll"                  },
    { IDR_DLL_AVCODEC,              L"avcodec-62.dll"                  },
    { IDR_DLL_AVDEVICE,             L"avdevice-62.dll"                 },
    { IDR_DLL_AVFILTER,             L"avfilter-11.dll"                 },
    { IDR_DLL_AVFORMAT,             L"avformat-62.dll"                 },
    { IDR_DLL_AVUTIL,               L"avutil-60.dll"                   },
    { IDR_DLL_DATE_TZ,              L"date-tz.dll"                     },
    { IDR_DLL_JPEG62,               L"jpeg62.dll"                      },
    { IDR_DLL_LIBCURL,              L"libcurl.dll"                     },
    { IDR_DLL_LIBLZMA,              L"liblzma.dll"                     },
    { IDR_DLL_LIBPNG16,             L"libpng16.dll"                    },
    { IDR_DLL_LIBPROTOBUF_LITE,     L"libprotobuf-lite.dll"            },
    { IDR_DLL_LIBPROTOBUF,          L"libprotobuf.dll"                 },
    { IDR_DLL_LIBPROTOC,            L"libprotoc.dll"                   },
    { IDR_DLL_LIBSHARPYUV,          L"libsharpyuv.dll"                 },
    { IDR_DLL_LIBWEBP,              L"libwebp.dll"                     },
    { IDR_DLL_LIBWEBPDECODER,       L"libwebpdecoder.dll"              },
    { IDR_DLL_LIBWEBPDEMUX,         L"libwebpdemux.dll"                },
    { IDR_DLL_LIBWEBPMUX,           L"libwebpmux.dll"                  },
    { IDR_DLL_ONNXRUNTIME,          L"onnxruntime.dll"                 },
    { IDR_DLL_ONNXRUNTIME_PROVIDERS,L"onnxruntime_providers_shared.dll"},
    { IDR_DLL_OPENCV_CALIB3D,       L"opencv_calib3d4.dll"             },
    { IDR_DLL_OPENCV_CORE,          L"opencv_core4.dll"                },
    { IDR_DLL_OPENCV_DNN,           L"opencv_dnn4.dll"                 },
    { IDR_DLL_OPENCV_FEATURES2D,    L"opencv_features2d4.dll"          },
    { IDR_DLL_OPENCV_FLANN,         L"opencv_flann4.dll"               },
    { IDR_DLL_OPENCV_HIGHGUI,       L"opencv_highgui4.dll"             },
    { IDR_DLL_OPENCV_IMGCODECS,     L"opencv_imgcodecs4.dll"           },
    { IDR_DLL_OPENCV_IMGPROC,       L"opencv_imgproc4.dll"             },
    { IDR_DLL_OPENCV_ML,            L"opencv_ml4.dll"                  },
    { IDR_DLL_OPENCV_OBJDETECT,     L"opencv_objdetect4.dll"           },
    { IDR_DLL_OPENCV_PHOTO,         L"opencv_photo4.dll"               },
    { IDR_DLL_OPENCV_STITCHING,     L"opencv_stitching4.dll"           },
    { IDR_DLL_OPENCV_VIDEO,         L"opencv_video4.dll"               },
    { IDR_DLL_OPENCV_VIDEOIO,       L"opencv_videoio4.dll"             },
    { IDR_DLL_PDCURSES,             L"pdcurses.dll"                    },
    { IDR_DLL_PKGCONF,              L"pkgconf-7.dll"                   },
    { IDR_DLL_RE2,                  L"re2.dll"                         },
    { IDR_DLL_SWRESAMPLE,           L"swresample-6.dll"                },
    { IDR_DLL_SWSCALE,              L"swscale-9.dll"                   },
    { IDR_DLL_TIFF,                 L"tiff.dll"                        },
    { IDR_DLL_TURBOJPEG,            L"turbojpeg.dll"                   },
    { IDR_DLL_ZLIB1,                L"zlib1.dll"                       },
};

// ── Resource helpers ──────────────────────────────────────────────────────────
static bool WriteResourceToFile(HMODULE hMod, int resId, const wchar_t* dest) {
    HRSRC   hRes  = FindResourceW(hMod, MAKEINTRESOURCEW(resId), RT_RCDATA);
    HGLOBAL hGlob = hRes ? LoadResource(hMod, hRes) : NULL;
    const void* p = hGlob ? LockResource(hGlob) : nullptr;
    DWORD   size  = hRes  ? SizeofResource(hMod, hRes) : 0;
    if (!p || !size) return false;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(dest, GetFileExInfoStandard, &a) &&
        a.nFileSizeHigh == 0 && a.nFileSizeLow == size) return true;
    HANDLE h = CreateFileW(dest, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(h, p, size, &written, NULL);
    CloseHandle(h);
    return written == size;
}

static bool ExtractEmbeddedDLLs() {
    CreateDirectoryW(DLL_DIR, NULL);
    HMODULE hMod = GetModuleHandleW(NULL);
    for (auto& dll : g_dlls) {
        wchar_t dest[MAX_PATH];
        swprintf_s(dest, L"%s%s", DLL_DIR, dll.name);
        if (!WriteResourceToFile(hMod, dll.id, dest)) return false;
    }
    SetDllDirectoryW(DLL_DIR);
    return true;
}

static bool LoadKernelDriver() {
    HMODULE hMod = GetModuleHandleW(NULL);
    HRSRC hResSys = FindResourceW(hMod, MAKEINTRESOURCEW(IDR_DRIVER_SYS), RT_RCDATA);
    HRSRC hResMap = FindResourceW(hMod, MAKEINTRESOURCEW(IDR_MAPPER_EXE), RT_RCDATA);
    if (!hResSys || !hResMap) return false;
    HGLOBAL gs = LoadResource(hMod, hResSys), gm = LoadResource(hMod, hResMap);
    const void* ps = LockResource(gs), *pm = LockResource(gm);
    DWORD ss = SizeofResource(hMod,hResSys), sm = SizeofResource(hMod,hResMap);
    if (!ps || !pm || !ss || !sm) return false;
    wchar_t td[MAX_PATH], sp[MAX_PATH], mp[MAX_PATH];
    GetTempPathW(MAX_PATH, td);
    DWORD rnd = GetTickCount() ^ (DWORD)(ULONG_PTR)GetCurrentProcess();
    swprintf_s(sp, L"%s%08X.sys", td, rnd);
    swprintf_s(mp, L"%s%08X.exe", td, rnd ^ 0xA5B3C7D1u);
    HANDLE h; DWORD w=0;
    h = CreateFileW(sp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (h==INVALID_HANDLE_VALUE) return false;
    WriteFile(h,ps,ss,&w,NULL); CloseHandle(h);
    if (w!=ss){DeleteFileW(sp);return false;}
    h = CreateFileW(mp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (h==INVALID_HANDLE_VALUE){DeleteFileW(sp);return false;}
    w=0; WriteFile(h,pm,sm,&w,NULL); CloseHandle(h);
    if (w!=sm){DeleteFileW(sp);DeleteFileW(mp);return false;}
    wchar_t cmd[MAX_PATH*2+8];
    swprintf_s(cmd, L"\"%s\" \"%s\"", mp, sp);
    STARTUPINFOW si={sizeof(si)}; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi={};
    BOOL ok=CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi);
    if (ok){WaitForSingleObject(pi.hProcess,15000);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);}
    DeleteFileW(sp); DeleteFileW(mp);
    return ok;
}

// Verifica se o driver já está carregado e respondendo (escreve heartbeat + noop,
// aguarda até 300ms pelo driver deletar o arquivo de comando)
static bool IsDriverAlive() {
    // Escreve heartbeat para tirar driver do dormant
    HANDLE hHb = CreateFileW(HEARTBEAT_FILE, GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hHb != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(hHb, "alive", 5, &w, NULL);
        CloseHandle(hHb);
    }
    // Envia noop (x=0 y=0 mode=0)
    HANDLE hCmd = CreateFileW(L"C:\\Windows\\Temp\\wmi_ipc.dat",
                              GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hCmd == INVALID_HANDLE_VALUE) return false;
    DWORD w; WriteFile(hCmd, "0 0 0", 5, &w, NULL);
    CloseHandle(hCmd);
    // Aguarda até 300ms — driver ativo deleta o arquivo em ~1ms
    for (int i = 0; i < 30; i++) {
        Sleep(10);
        if (GetFileAttributesW(L"C:\\Windows\\Temp\\wmi_ipc.dat") == INVALID_FILE_ATTRIBUTES)
            return true;
    }
    DeleteFileW(L"C:\\Windows\\Temp\\wmi_ipc.dat");
    return false;
}

// ── Console / Signal ──────────────────────────────────────────────────────────
static void EnableAnsiConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    SetConsoleCP(65001); SetConsoleOutputCP(65001);
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    _setmode(_fileno(stdout), _O_BINARY);
}

static BOOL WINAPI CtrlHandler(DWORD sig) {
    if (sig == CTRL_C_EVENT || sig == CTRL_CLOSE_EVENT) { g_running = false; return TRUE; }
    return FALSE;
}

// Controlado pelo loop principal: pausa heartbeat durante scan de startup do Vanguard
std::atomic<bool> g_heartbeat_pause{ false };

static void HeartbeatThread() {
    AD_HideThread();
    while (g_running) {
        if (g_heartbeat_pause.load()) {
            // Vanguard fazendo scan — apaga heartbeat e dorme (driver fica dormant)
            DeleteFileW(HEARTBEAT_FILE);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        HANDLE h = CreateFileW(HEARTBEAT_FILE, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; WriteFile(h,"alive",5,&w,NULL); CloseHandle(h);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    DeleteFileW(HEARTBEAT_FILE);
}

// ── Apply config to running components ───────────────────────────────────────
static void ApplyConfig(const IrisConfig& c, YOLOInference& yolo, MouseController& mouse) {
    yolo.SetConfidenceThreshold(c.confidence);
    mouse.SetDamping(c.smoothing);
    mouse.SetDeadzone(c.deadzone);
    mouse.SetMaxSpeed(c.max_speed);
    mouse.SetOvershooting(c.overshooting);
}

// ── Entry Point ───────────────────────────────────────────────────────────────
int main()
{
    AntiDebugInit();
    AntiDebugCheck();
    EnableAnsiConsole();
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    // ── [1] Extract DLLs ─────────────────────────────────────────────────
    printf(C_INFO "  [*] " C_RST "Extracting runtime...\n");
    if (!ExtractEmbeddedDLLs()) {
        printf(C_ERR "  [-] " C_RST "Failed to extract DLLs\n");
        printf("  Press any key to exit...\n"); _getch(); return 1;
    }
    printf(C_OK "  [+] " C_RST "Runtime ready\n");

    // ── [2] Kernel driver ────────────────────────────────────────────────
    // Verifica se driver já está ativo (run anterior sem fechar corretamente)
    if (IsDriverAlive()) {
        printf(C_OK "  [+] " C_RST "Driver already loaded\n");
    } else {
        printf(C_INFO "  [*] " C_RST "Loading driver...\n");
        if (!LoadKernelDriver()) {
            printf(C_ERR "  [-] " C_RST "Driver load failed\n");
            printf("  Press any key to exit...\n"); _getch(); return 1;
        }
        printf(C_OK "  [+] " C_RST "Driver loaded\n");
    }

    // ── [3] Load saved config ────────────────────────────────────────────
    IrisConfig cfg;
    LoadConfig(cfg);

    // ── [4] Screen Capture ───────────────────────────────────────────────
    // Retry loop: DuplicateOutput falha enquanto jogo está em exclusive fullscreen
    printf(C_INFO "  [*] " C_RST "Initializing screen capture...\n");
    DXGICaptureOptimized capture;
    {
        int attempts = 0;
        while (!capture.Initialize(0)) {
            if (++attempts >= 30) {  // até 60s de retry
                printf(C_ERR "  [-] " C_RST "DXGI capture failed after retries\n");
                printf("  Press any key to exit...\n"); _getch(); return 1;
            }
            printf(C_INFO "  [*] " C_RST "DXGI unavailable, retrying in 2s... (%d/30)\r", attempts);
            Sleep(2000);
        }
    }
    printf(C_OK "  [+] " C_RST "Capture ready  screen=%dx%d  ROI=%dx%d\n",
           capture.GetWidth(), capture.GetHeight(),
           capture.GetROIWidth(), capture.GetROIHeight());

    // ── [5] YOLO Model ───────────────────────────────────────────────────
    printf(C_INFO "  [*] " C_RST "Loading YOLO model...\n");
    YOLOInference yolo;
    {
        HRSRC hRes    = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_BEST_ONNX), RT_RCDATA);
        HGLOBAL hGlob = hRes ? LoadResource(NULL, hRes) : NULL;
        const void* pData = hGlob ? LockResource(hGlob) : nullptr;
        DWORD dataSize    = hRes  ? SizeofResource(NULL, hRes) : 0;
        if (!pData || !dataSize || !yolo.LoadModelFromMemory(pData, dataSize)) {
            printf(C_ERR "  [-] " C_RST "Failed to load model\n");
            printf("  Press any key to exit...\n"); _getch(); return 1;
        }
    }
    yolo.SetConfidenceThreshold(cfg.confidence);
    printf(C_OK "  [+] " C_RST "YOLO model loaded (%dx%d)\n",
           yolo.GetInputWidth(), yolo.GetInputHeight());

    // ── [6] Mouse Controller ─────────────────────────────────────────────
    MouseController mouse(
        capture.GetWidth(),    capture.GetHeight(),
        capture.GetROIX(),     capture.GetROIY(),
        capture.GetROIWidth(), capture.GetROIHeight(),
        cfg.fov_h, cfg.fov_v, cfg.sensitivity, cfg.dpi
    );
    mouse.SetDamping(cfg.smoothing);
    mouse.SetDeadzone(cfg.deadzone);
    mouse.SetMaxSpeed(cfg.max_speed);
    mouse.SetOvershooting(cfg.overshooting);
    printf(C_OK "  [+] " C_RST "Mouse controller ready\n");

    // ── [7] Overlay window ───────────────────────────────────────────────
    Overlay::Init();

    // ── [8] Heartbeat ────────────────────────────────────────────────────
    std::thread hbThread(HeartbeatThread);

    // ── Config always visible — render once after model is up ─────────────
    CfgHideCursor();
    int  cfg_inner = std::min(CfgConsoleW() - 8, 68);
    int  cfg_sel   = 0;
    printf("\n");
    RenderConfig(cfg, cfg_sel, cfg_inner, /*first_render=*/true, g_aiming.load());

    // ── Main Loop ─────────────────────────────────────────────────────────
    cv::Mat frame;
    std::vector<YOLOInference::Detection> detections;
    auto fps_start = std::chrono::high_resolution_clock::now();
    int  fps_count = 0;
    while (g_running) {
        // ── F1 toggle via polling (sem WH_KEYBOARD_LL global hook) ───────────
        if (PollF1Toggle()) {
            g_aiming = !g_aiming.load();
            Overlay::SetVisible(g_aiming.load() && cfg.show_boxes);
            RenderConfig(cfg, cfg_sel, cfg_inner, false, g_aiming.load());
        }

        // ── Setas / R — apenas quando console está focado ─────────────────
        if (_kbhit()) {
            int ch = _getch();
            bool redraw    = false;
            bool do_apply  = false;

            if (ch == 0 || ch == 0xE0) {
                int code = _getch();
                switch (code) {
                case 72: cfg_sel = (cfg_sel - 1 + ROW_COUNT) % ROW_COUNT; redraw = true; break;
                case 80: cfg_sel = (cfg_sel + 1) % ROW_COUNT;             redraw = true; break;
                case 75: AdjustRow(cfg, cfg_sel, -1); redraw = do_apply = true; break;
                case 77: AdjustRow(cfg, cfg_sel, +1); redraw = do_apply = true; break;
                }
            }
            else if (ch == 'r' || ch == 'R') {
                cfg = IrisConfig{};
                redraw = do_apply = true;
            }

            if (do_apply) {
                SaveConfig(cfg);
                ApplyConfig(cfg, yolo, mouse);
                if (cfg_sel == ROW_ROI) {
                    capture.Reinitialize(kROISizes[cfg.roi_idx]);
                    mouse.SetROI(capture.GetROIX(), capture.GetROIY(),
                                 capture.GetROIWidth(), capture.GetROIHeight());
                    Overlay::ShowROI(capture.GetROIX(), capture.GetROIY(),
                                     capture.GetROIWidth(), capture.GetROIHeight());
                } else {
                    Overlay::SetVisible(g_aiming.load() && cfg.show_boxes);
                }
            }
            if (redraw)
                RenderConfig(cfg, cfg_sel, cfg_inner, false, g_aiming.load());
        }

        // ── Modo dormente: libera DXGI quando Valorant fecha ─────────────────
        // Evita que Vanguard detecte handle DXGI ativo no re-launch do jogo
        {
            // Inicializa com estado real — evita entrar em dormant se Valorant não
            // estava rodando quando Iris iniciou
            static bool s_valorant_was_running = IsValorantRunning();
            static auto s_valorant_closed_at   = std::chrono::steady_clock::now();
            static bool s_dormant              = false;
            static bool s_waiting_reactivate   = false;

            bool val_running = IsValorantRunning();

            if (s_valorant_was_running && !val_running) {
                // Valorant fechou — entra em modo dormente, libera DXGI e pausa heartbeat
                capture.Release();
                Overlay::Destroy();
                g_heartbeat_pause      = true;   // driver entra em dormant também
                s_dormant              = true;
                s_waiting_reactivate   = false;
                s_valorant_closed_at   = std::chrono::steady_clock::now();
                s_valorant_was_running = false;
            }
            else if (s_dormant && val_running && !s_waiting_reactivate) {
                // Valorant reabriu — heartbeat continua pausado durante os 30s de wait
                s_waiting_reactivate   = true;
                s_valorant_closed_at   = std::chrono::steady_clock::now();
                s_valorant_was_running = true;
            }
            else if (!s_valorant_was_running && val_running) {
                // Valorant abriu pela primeira vez (sem ciclo dormant) — só atualiza estado
                s_valorant_was_running = true;
            }
            else if (s_waiting_reactivate) {
                auto waited = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - s_valorant_closed_at).count();
                if (waited >= 30) {
                    // Reativa tudo após 30s — Vanguard terminou scan de startup
                    g_heartbeat_pause = false;   // driver volta ao modo ativo
                    Overlay::Init();
                    if (capture.Initialize(0)) {
                        mouse.SetROI(capture.GetROIX(), capture.GetROIY(),
                                     capture.GetROIWidth(), capture.GetROIHeight());
                        s_dormant            = false;
                        s_waiting_reactivate = false;
                    } else {
                        // DuplicateOutput ainda falhou — tenta de novo em 5s
                        s_valorant_closed_at = std::chrono::steady_clock::now() - std::chrono::seconds(25);
                    }
                }
            }

            if (s_dormant || s_waiting_reactivate) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        // ── Inference ─────────────────────────────────────────────────────
        if (!capture.GetLatestBGR(frame)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        detections.clear();
        if (!yolo.RunInference(frame, detections)) continue;

        // ── Target selection ──────────────────────────────────────────────
        static float s_last_tx = 0.5f, s_last_ty = 0.5f;  // centro raw (sem offset) para score
        static bool  s_has_target          = false;
        static int   s_lost_frames         = 0;       // frames consecutivos sem detecção
        static constexpr int GRACE_FRAMES  = 3;       // ~25ms a 120fps — menos lag em alvo móvel
        static int   s_af_confirm          = 0;       // frames consecutivos com detecção (anti-FP)
        static bool  s_af_confirmed        = false;   // passou do mínimo de frames
        static bool  s_af_seen_set         = false;
        static bool  s_af_shot_this_session= false;
        static auto  s_af_first_seen       = std::chrono::steady_clock::now();
        static auto  s_af_last_click       = std::chrono::steady_clock::now();

        const YOLOInference::Detection* best = nullptr;

        if (g_aiming.load() && !detections.empty()) {
            float best_score = FLT_MAX;

            // Fase 1: tipo preferido (head quando priority==1, body quando priority==0)
            for (const auto& det : detections) {
                bool is_head = (det.class_id == 0);
                if (cfg.priority == 0 &&  is_head) continue;
                if (cfg.priority == 1 && !is_head) continue;

                // Score usa centro raw da box (s_last_tx = raw, sem offset de aim)
                float dx_last   = det.cx - s_last_tx,  dy_last   = det.cy - s_last_ty;
                float dx_center = det.cx - 0.5f,        dy_center = det.cy - 0.5f;
                float score = (dx_last*dx_last + dy_last*dy_last) * (s_has_target ? 0.6f : 0.0f)
                            + (dx_center*dx_center + dy_center*dy_center) * 0.4f;
                if (is_head) score *= 0.5f;
                if (score < best_score) { best_score = score; best = &det; }
            }

            // Nota: priority==1 (Head) não tem fallback para body — o grace period
            // (s_lost_frames < GRACE_FRAMES) já mantém a última posição da cabeça
            // durante os frames em que a detecção cai, evitando que a mira desça para o body.
        }

        if (g_aiming.load()) {
            if (best) {
                // ── Detecção ativa ────────────────────────────────────────
                // Salva centro raw para score de persistência no próximo frame
                s_last_tx     = best->cx;
                s_last_ty     = best->cy;
                s_has_target  = true;
                s_lost_frames = 0;

                // Offset vertical: 2px abaixo do centro da head (só para o mouse)
                float aim_x = best->cx;
                float aim_y = best->cy;
                if (best->class_id == 0) {
                    aim_y += 2.0f / (float)capture.GetROIHeight();
                }
                mouse.MoveToTarget(aim_x, aim_y);

                // Confirmação anti-falso-positivo: 2 frames suficientes para resposta rápida
                if (s_af_confirm < 2) {
                    s_af_confirm++;
                } else if (!s_af_confirmed) {
                    s_af_confirmed         = true;
                    s_af_seen_set          = true;
                    s_af_shot_this_session = false;
                    s_af_first_seen        = std::chrono::steady_clock::now();
                }

            } else if (s_has_target && s_lost_frames < GRACE_FRAMES) {
                // ── Grace period: detecção sumiu mas ainda dentro do limite ──
                // Mantém mira na última posição conhecida e sessão de autofire viva
                s_lost_frames++;
                mouse.MoveToTarget(s_last_tx, s_last_ty);
                // s_af_confirm e s_af_confirmed permanecem intactos

            } else {
                // ── Realmente perdeu o alvo ───────────────────────────────
                s_has_target           = false;
                s_lost_frames          = 0;
                s_af_confirm           = 0;
                s_af_confirmed         = false;
                s_af_seen_set          = false;
                s_af_shot_this_session = false;
            }

            // ── Autofire (tap mode) — roda sempre que sessão está confirmada ──
            if (cfg.autofire && s_af_confirmed) {
                auto  now_af        = std::chrono::steady_clock::now();
                float since_seen    = std::chrono::duration<float>(now_af - s_af_first_seen).count();
                float since_click   = std::chrono::duration<float>(now_af - s_af_last_click).count();
                float initial_s     = cfg.autofire_initial * 0.1f;
                float shoot_delay_s = cfg.autofire_delay   * 0.1f;

                if (since_seen >= initial_s) {
                    if (!s_af_shot_this_session) {
                        mouse.SendClick();
                        s_af_last_click        = now_af;
                        s_af_shot_this_session = true;
                    } else if (since_click >= shoot_delay_s) {
                        mouse.SendClick();
                        s_af_last_click = now_af;
                    }
                }
            }
        }

        // ── Overlay boxes ─────────────────────────────────────────────────
        if (cfg.show_boxes && g_aiming.load()) {
            int rx=capture.GetROIX(), ry=capture.GetROIY();
            int rw=capture.GetROIWidth(), rh=capture.GetROIHeight();
            std::vector<OverlayBox> boxes;
            for (const auto& det : detections) {
                bool is_head = (det.class_id == 0);  // class 0=head, class 1=body
                if (cfg.priority == 1 && !is_head) continue;  // head only
                if (cfg.priority == 0 &&  is_head) continue;  // body only
                // priority == 2: head+body — ambos passam, head tem bônus de score (×0.5)
                OverlayBox b;
                b.x = rx + (int)((det.cx - det.w*0.5f)*rw);
                b.y = ry + (int)((det.cy - det.h*0.5f)*rh);
                b.w = (int)(det.w*rw);
                b.h = (int)(det.h*rh);
                boxes.push_back(b);
            }
            Overlay::Update(boxes);
            Overlay::SetVisible(true);
        } else {
            Overlay::SetVisible(false);
        }

        // ── FPS counter ───────────────────────────────────────────────────
        fps_count++;
        if (fps_count % 500 == 0) AntiDebugCheck();
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - fps_start).count();
        if (elapsed >= 1.0f) {
            float fps = fps_count / elapsed;
            char title[64];
            snprintf(title, sizeof(title), "System Performance Monitor — %.0f%%",
                     fps * 0.5f);
            SetConsoleTitleA(title);
            // Mostra FPS na mesma linha do console (não quebra layout do config)
            printf("\033[s\033[1;1H\033[K" C_INFO "  FPS: %.0f  ms/frame: %.1f" C_RST "\033[u",
                   fps, 1000.0f / fps);
            fflush(stdout);
            fps_count = 0;
            fps_start = now;
        }
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    Overlay::Shutdown();
    capture.Stop();
    g_running = false;

    // Sinaliza driver para parar antes de encerrar o heartbeat
    // Sem isso, Driver.sys fica residente no kernel e detectável no re-launch
    {
        HANDLE hCmd = CreateFileW(
            L"C:\\Windows\\Temp\\wmi_ipc.dat",
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hCmd != INVALID_HANDLE_VALUE) {
            const char* stopCmd = "0 0 -1";
            DWORD w = 0;
            WriteFile(hCmd, stopCmd, (DWORD)strlen(stopCmd), &w, NULL);
            CloseHandle(hCmd);
            // Aguarda até 3s para driver confirmar (apagar o arquivo)
            for (int i = 0; i < 30; i++) {
                Sleep(100);
                if (GetFileAttributesW(L"C:\\Windows\\Temp\\wmi_ipc.dat")
                    == INVALID_FILE_ATTRIBUTES) break;
            }
        }
    }

    // Limpa arquivos residuais que o Vanguard pode usar como assinatura
    DeleteFileW(L"C:\\Windows\\Temp\\driver_debug.log");

    // Limpa pasta de DLLs extraídas (re-extraídas no próximo launch)
    {
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(L"C:\\Windows\\Temp\\WinDiag\\*", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    wchar_t fp[MAX_PATH];
                    swprintf_s(fp, L"C:\\Windows\\Temp\\WinDiag\\%s", fd.cFileName);
                    DeleteFileW(fp);
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
            RemoveDirectoryW(L"C:\\Windows\\Temp\\WinDiag\\");
        }
    }

    hbThread.join();
    return 0;
}
