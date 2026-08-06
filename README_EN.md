# ApplepieManager

English | [中文](README.md)

A DLL plugin manager for *Arknights: Endfield*. Provides a unified plugin loading, hotkey management, and configuration interface with multilingual support. Displays a GUI via an independent DX11 overlay window. Includes built-in UID hiding for installation verification, allowing one-click hiding of UIDs and player names from the in-game HUD, menus, and business cards.

## Key Features

- **Plugin Management**: Automatically scans DLL plugins in the `plugin\` directory with enable/disable, pause/resume controls and real-time status display
- **Hotkey Management**: Centralized hotkey overview for all plugins with conflict detection and one-click rebinding
- **UID Hiding**: Hide in-game UID, ping display, menu UID/name, and business card UID/name
- **Multilingual UI**: One-click Chinese/English toggle that broadcasts the language to all supporting plugins

## User Agreement & Disclaimer

<details>
<summary>Please read this agreement carefully before downloading, installing, or using this plugin. <b>By using this plugin, you acknowledge that you have fully read, understood, and agreed to all of the following terms.</b></summary>

### 1. Open Source License & End User Rights
- This plugin is fully open-sourced on GitHub under the **AGPL-3.0** license. Users may freely use, modify, and distribute the source code of this plugin in compliance with the license.
- End Users may use and distribute this plugin **without any restrictions**, provided they do not modify it. This right is not affected by whether the user violates this agreement.

### 2. Anti-Fraud Statement
- You **must not** openly sell this plugin **itself** on online retail platforms without providing the GitHub repository address and after-sales service.
- This plugin is entirely free and open-source on GitHub. If you obtained it through a paid purchase, please be aware that it is freely available on GitHub.

### 3. Content Compliance & Conduct
- This plugin does not contain any game art assets. Users acknowledge and agree that the official animations, scenes, models, and other assets built into *Arknights: Endfield* are copyrighted by Hypergryph and are not covered by the AGPL-3.0 license.

### 4. Risk & Disclaimer
- This project is for educational, technical research, and communication purposes only. All Arknights game data assets used in this plugin are copyrighted by Hypergryph. Using this tool may violate the game's terms of service and carries a risk of account suspension. For any loss directly or indirectly caused by using this plugin (including but not limited to account bans, game data corruption, etc.), **this project assumes no legal or financial liability**. Users bear all risks and are strongly advised to use it on a test account.

</details>

## Installation

Copy the following files to the game directory (same folder as `Endfield.exe`):

```
bin/applepie_manager.dll   → Game Directory/plugin/applepie_manager.dll
bin/vulkan-1.dll           → Game Directory/vulkan-1.dll
bin/d3dcompiler_47.dll     → Game Directory/d3dcompiler_47.dll
```

> **Note**: `d3dcompiler_47.dll` (DirectX) and `vulkan-1.dll` (Vulkan) are proxy loaders. You only need the one matching your rendering API, or both. If you are also using other plugins that share these proxy loaders, there is no need to overwrite them.

> If you have **never installed a plugin of this type before**, you may need to create the `plugin` folder yourself.

## Usage

1. Launch the game after installing the files as described above.
2. Press the `HOME` key to open/close the manager panel.
3. The panel lets you view and manage all loaded plugins, adjust UID hiding options, and rebind hotkeys.
4. Click the language toggle button to switch languages.

## Plugin Development

If you want to develop plugins that integrate with Applepie Manager, see the [Plugin Development Guide (applepie.md)](applepie.md).
