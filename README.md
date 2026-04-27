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

Iris uses a YOLOv8 model to detect players in real time and moves your mouse toward the closest target. Everything runs inside a single terminal window with a clean purple UI — no overlays, no second windows.

## Features

- **YOLOv8 detection** via ONNX Runtime with DirectML (GPU acceleration on any DirectX 11 card)
- **DXGI screen capture** — low-latency, no performance impact on the game
- **Kernel-mode mouse driver** — movement bypasses Win32 entirely
- **Token-gated** — requires a valid key to launch
- **Auto-updating** — always downloads the latest version at startup, no manual updates needed
- **Interactive config** — tune every aiming parameter from the terminal before each session

## Configuration

When Iris launches, an interactive screen lets you adjust all settings with arrow keys. Changes are saved automatically for next time.

| Setting | Default | Description |
|---------|---------|-------------|
| Confidence Threshold | `0.75` | Minimum detection confidence |
| Head Priority | `0.50` | How much to prefer head over body |
| FOV Horizontal | `103°` | Your in-game horizontal FOV |
| FOV Vertical | `70°` | Your in-game vertical FOV |
| Sensitivity | `1.0` | Your in-game sensitivity |
| DPI | `800` | Your mouse DPI |
| Smoothing | `0.85` | Movement smoothness (0 = instant) |
| Deadzone | `3.0°` | Minimum angle before moving |
| Max Speed | `500 px/s` | Movement speed cap |

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
