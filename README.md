<div align="center">

```
██╗██████╗ ██╗███████╗
██║██╔══██╗██║██╔════╝
██║██████╔╝██║███████╗
██║██╔══██╗██║╚════██║
██║██║  ██║██║███████║
╚═╝╚═╝  ╚═╝╚═╝╚══════╝
```

**Computer vision aim assist for Valorant**

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue?style=flat-square)
![Language](https://img.shields.io/badge/language-C%2B%2B17-purple?style=flat-square)
![GPU](https://img.shields.io/badge/inference-DirectML-orange?style=flat-square)

</div>

---

Iris uses a YOLO26s model to detect players in real time and moves your mouse toward the closest target. Everything runs inside a single terminal window with a clean purple UI — no overlays, no second windows.

## Features

- **YOLO26s detection** via ONNX Runtime with DirectML (GPU acceleration on any DirectX 11 card)
- **DXGI screen capture** — low-latency, no performance impact on the game
- **Kernel-mode mouse driver** — movement bypasses Win32 entirely
- **Token-gated** — requires a valid key to launch
- **Auto-updating** — always downloads the latest version at startup, no manual updates needed
- **Interactive config** — tune every aiming parameter from the terminal before each session

## Configuration

When Iris launches, an interactive screen lets you adjust all settings with arrow keys. Changes are saved automatically for next time.

| Setting | Default | Description |
|---------|---------|-------------|
| Confidence Threshold | `0.65` | Minimum detection confidence to track |
| Priority | `Head` | Target preference: Body / Head / Body+Head |
| ROI Size | `960` | Capture region size in pixels (centered on screen) |
| Show Boxes | `Off` | Overlay detection boxes on screen |
| Autofire | `On` | Automatically shoot when on target |
| Autofire Initial Delay | `0.2s` | Delay before the first shot |
| Autofire Delay | `0.2s` | Delay between consecutive shots |
| Overshooting | `70` | Mouse movement strength factor |
| FOV Horizontal | `103°` | Your in-game horizontal FOV |
| FOV Vertical | `70°` | Your in-game vertical FOV |
| Sensitivity | `1.0` | Your in-game sensitivity |
| DPI | `800` | Your mouse DPI |
| Smoothing | `Off` | Movement damping (0 = instant) |
| Deadzone | `Off` | Minimum angle before movement is applied |
| Max Speed | `1600 px/s` | Maximum movement speed cap |

## Requirements

- Windows 10 / 11 (64-bit)
- Administrator privileges
- Core Isolation (Memory Integrity) **disabled**
- Windows Defender real-time protection **disabled**
- Valorant / Vanguard **not running** at launch

Iris checks all of these automatically and tells you exactly what to fix before proceeding.

## Getting Access

Join our Discord and grab a token. Paste it into Iris on first launch — that's it.

---

<div align="center">
<sub>dc: @d.zn.</sub>
</div>
