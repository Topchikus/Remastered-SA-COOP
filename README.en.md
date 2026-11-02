<p align="center">
  <img src="docs/images/logo.png" alt="Remastered SA:COOP" width="440">
</p>

<p align="center">
  <a href="https://github.com/Topchikus/Remastered-SA-COOP/releases/latest"><img src="https://img.shields.io/github/v/release/Topchikus/Remastered-SA-COOP" alt="Release"></a>
  <a href="https://github.com/Topchikus/Remastered-SA-COOP/releases"><img src="https://img.shields.io/github/downloads/Topchikus/Remastered-SA-COOP/total" alt="Downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/Topchikus/Remastered-SA-COOP" alt="MIT License"></a>
  <img src="https://img.shields.io/badge/GTA_San_Andreas-1.0_US-orange" alt="GTA San Andreas 1.0 US">
  <img src="https://img.shields.io/badge/Players-Up_to_16-brightgreen" alt="Up to 16 players">
</p>

<p align="center"><a href="README.md">Русский</a> · <b>English</b></p>

**Remastered SA:COOP** is an enhanced co-op multiplayer mod for GTA San Andreas.
It combines the official release of **SA:COOP v0.3.2** (by Akbar Karimov), network architecture supporting **up to 16 players** (by Farus256), and custom gameplay systems and fixes by **Topchikus** (duels with GUI & bets, 360+ animation menu, safe cutscene spawning, and graphic stability).

![Host and partner on Grove Street](docs/images/photo_2026-10-26_12-38-37.jpg)

## 🌟 Features of Remastered Edition

### 👥 Up to 16 Players Support
* Dedicated server and protocol architecture supporting **up to 16 players**.
* Intuitive player count selector (2 to 16) in the "Create Server" tab (**F9**).
* No false rejections ("server is full").

### ⚔️ Duel System (`/duel` or `/дуэль`)
* Interactive GUI window to select opponent, weapon, bets ($0 - $100,000), and 5 distinct duel arenas.
* 3..2..1..GO countdown with frozen movement.
* Nametags and overhead weapon icons are automatically hidden during duels for fair competitive PvP.
* Automatic teleport back to your previous spot after the duel.

### 💃 Animation Menu (`/anim` or `/аним`)
* Graphical menu featuring 15 categories and over **360 animations**.
* Instant search by name.
* Press **Space** or **F** to stop playing any animation immediately.

### 🎬 Safe Cutscene Spawning & Collision Fixes
* Fixed the bug where partners fell through the map or spawned inside other players/vehicles after cutscenes (such as "Just Business").
* Partners spawn safely beside the host on solid ground (`FindGroundZFor3DCoord`) with geometry preloading (`LOAD_SCENE`).

### 🖥️ Graphics Stability & UltraWide Support
* Clean original rendering without map flickering, missing roads, or LOD bugs.
* Supports both classic Fullscreen mode (with standard in-game resolution selector) and Borderless Windowed mode for any display size (including 3440x1440).

---

## 📥 Installation

Requires GTA San Andreas **v1.0 US** (clean or modded).

1. Download `SA-COOP-Duels-Anims-GUI.zip` from [Releases](https://github.com/Topchikus/Remastered-SA-COOP/releases).
2. Extract the archive into your GTA San Andreas root folder (so `coop.asi` and `coop` folder are next to `gta_sa.exe`).
3. Run `gta_sa.exe`. Press **F9** (or type `/меню`) to open the co-op menu, enter your nickname, and connect!

---

## ⌨️ Chat Commands (press T)

* `/duel [ID]` — open the duels menu with weapon, arena, and bet choices.
* `/anim` — open the animation menu (360+ animations in 15 categories).
* `/pay [ID] [amount]` — transfer money to another player.
* `/tp [ID]` — request teleport to a player (they answer with Y or N).
* `/tphere [ID]` — invite a player to teleport to you.
* `/blip [color]` — change your radar blip color.

---

## 👥 Credits

* **Akbar Karimov** ([sa-coop](https://github.com/Karimov-Akbar/sa-coop)) — author of the original SA:COOP mod.
* **Farus256** ([sa-coop-16players](https://github.com/Farus256/sa-coop-16players)) — author of up to 16 players architecture.
* **Topchikus** ([Remastered-SA-COOP](https://github.com/Topchikus/Remastered-SA-COOP)) — Remastered edition creator: v0.3.2 merge, duels system, animation menu, cutscene collision fixes, and release packaging.

## 📄 License

The code is distributed under the [MIT License](LICENSE).
GTA San Andreas is a trademark of Rockstar Games / Take-Two Interactive.
