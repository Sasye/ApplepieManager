# ApplepieManager

English | [中文](README.md)

A DLL plugin manager for *Arknights: Endfield*. Manager contains a Core for plugin loading, exclusive and typed shared hooks, managed pointer slots, window routing, asynchronous logical enable/disable and independent animation/camera ownership. It retains the independent DX11 panel and existing UID feature. See the [SDK](sdk/README.md) for plugin integration.

## Key Features

- **Plugin Management**: Scans after CoreReady, checks new manifests, ABI, duplicate IDs and required capabilities, and distinguishes loaded/attached/active states
- **Dynamic Hooks**: Exclusive and typed shared targets, two-stage activation, address deduplication, exact native signatures and SEH entry contracts; SDK in `sdk/`
- **Shared Services**: Conditional PointerPatch restoration, one game-window chain and minimal runtime/target-generation tickets
- **Lifecycle and Ownership**: Runtime buttons asynchronously load, enable and disable adapted clients; Disabled requires confirmed restoration, Enable grants no animation/camera ownership, and DLLs remain resident
- **Startup Configuration**: Panel checkboxes apply on the next launch, independently of runtime buttons
- **UID Hiding**: Hide in-game UID, ping display, menu UID/name, and business card UID/name
- **Multilingual UI**: Chinese/English toggle for the Manager panel

Optional plugin dependencies, versioned synchronous services, detailed errors and bounded log rotation are available in SDK 1.1, which remains compatible with 1.0 plugins. Built-in UID object operations run on the verified game thread. See the [SDK guide](sdk/README.md).

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

> `d3dcompiler_47.dll` (DirectX) and `vulkan-1.dll` (Vulkan) only bootstrap Manager. Either or both can be used. Plugins integrate through the SDK; legacy proxies, packages and hosts are not bridged.

> If you have **never installed a plugin of this type before**, you may need to create the `plugin` folder yourself.

## Usage

1. Launch the game after installing the files as described above.
2. Press the `HOME` key to open/close the manager panel.
3. Inspect load/Attach/capability states, set loading for the next launch, and adjust UID hiding options.
4. Click the language toggle button to switch languages.

## Plugin Development

Plugins can use the optional [C++ client](sdk/applepie_client.h) and [Endfield shared adapters](sdk/applepie_endfield_adapters.h); the [C API](sdk/applepie.h) remains available. See the [SDK guide](sdk/README.md) for integration and a complete example. Run `sdk\examples\build.bat` to build the minimal plugin, or add `/verify` for standalone offline checks. Neither command launches or deploys to the game.

For existing plugins, use the [Applepie plugin migration skill](skills/applepie-plugin-migration/SKILL.md) with your coding agent. It selects the required integration paths while preserving custom hooks, pointer-slot wrappers and business restoration.

Install the Visual Studio C++ desktop workload, then run `build.bat` from the project root. The production build generates `bin/applepie_manager.dll`, `bin/d3dcompiler_47.dll` and `bin/vulkan-1.dll`.
