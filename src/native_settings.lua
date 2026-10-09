-- Embedded in applepie_manager.dll; runs in the game's existing Lua VM.
-- Only Manager UI is implemented here. Plugins still use the C SDK.
-- Return a status even before the native command bridge is available.
for _, name in ipairs({"HL", "UIUtils", "LuaManagerInst", "UIManager", "PanelId", "GEnums", "require_ex"}) do
    if not _G[name] then return "waiting:" .. name end
end
if not PanelId.GameSetting then return "waiting:PanelId.GameSetting" end

local prefix = "@ApplepieMenu/v1/"
local function hex(s)
    return (tostring(s):gsub(".", function(c) return string.format("%02x", string.byte(c)) end))
end
local function send(command)
    -- This reserved path is a command, not a game resource. Its return is unused.
    LuaManagerInst:LoadLua(prefix .. command)
end
if _G.ApplepieNativeSettings then send("ready"); return "ready" end
local panelModule = require_ex("UI/Panels/GameSetting/GameSettingCtrl")
local baseModule = require_ex("UI/Panels/Base/UICtrl")
local popup = require_ex("UI/Panels/CommonPopUp/CommonPopUpCtrl").CommonPopUpCtrl
local itemWidget = require_ex("UI/Widgets/GameSettingItemCell")
assert(panelModule and panelModule.GameSettingCtrl, "GameSettingCtrl unavailable")
local ctrl = panelModule.GameSettingCtrl
local oldBuild, oldTab = ctrl._BuildSettingDataList, ctrl._RefreshSettingTab
local oldTabCell = ctrl._RefreshSettingTabCell
local oldHide, oldClose, oldShow = ctrl.OnHide, ctrl.OnClose, ctrl.OnShow
local oldPopupShow, oldPopupHide, oldPopupClose = popup._ShowPopUp, popup.OnHide, popup.OnClose
assert(oldBuild and oldTab and oldTabCell and itemWidget.InitGameSettingItemCell and ctrl._RefreshSettingItemCell
    and ctrl._DropdownAdjustExpandDirection and ctrl._SetSettingItemControllerNaviTarget, "Game settings contract changed")
assert(oldPopupShow and oldPopupHide and oldPopupClose and popup.AutoOpen, "Popup contract changed")
local tabId = "applepie_manager"
local active, data, page = nil, nil, -1
local chinese = true
local rows = {}
local capturePopup, restoreCaptureUI
local function text(zh, en) return chinese and zh or en end

-- Decode once on the game thread. Both native icon layers retain their own tint.
local iconTexture, iconSprite, iconAttempted
local function tabSprite()
    if iconAttempted then return iconSprite end
    iconAttempted = true
    local ok, iconError = pcall(function()
        local unity = CS.UnityEngine
        local bytes = CS.System.Convert.FromBase64String(applepieTabIconBase64)
        iconTexture = unity.Texture2D(2, 2, unity.TextureFormat.RGBA32, false)
        assert(unity.ImageConversion.LoadImage(iconTexture, bytes, true), "PNG decoding failed")
        iconTexture.name = "ApplepieManagerTabIcon"
        iconTexture.filterMode = unity.FilterMode.Bilinear
        iconTexture.wrapMode = unity.TextureWrapMode.Clamp
        iconSprite = unity.Sprite.Create(iconTexture, unity.Rect(0, 0, iconTexture.width, iconTexture.height),
            Vector2(0.5, 0.5), 100, 0, unity.SpriteMeshType.FullRect)
        assert(iconSprite, "Sprite creation failed")
        iconSprite.name = "ApplepieManagerTabIcon"
        -- Keep this one shared texture/sprite across menu closure and scene changes.
        iconTexture.hideFlags = unity.HideFlags.HideAndDontSave
        iconSprite.hideFlags = unity.HideFlags.HideAndDontSave
    end)
    if not ok then
        if iconSprite then CS.UnityEngine.Object.Destroy(iconSprite) end
        if iconTexture then CS.UnityEngine.Object.Destroy(iconTexture) end
        iconSprite, iconTexture = nil, nil
        send("icon-error/" .. hex(tostring(iconError):sub(1, 768)))
    end
    return iconSprite
end
local function pluginLabel(plugin)
    local color = plugin.state == "Active" and "6FCF97" or "EB5757"
    return plugin.name .. " · <color=#" .. color .. ">" .. plugin.state .. "</color>"
end
local function notice(content)
    popup.ShowPopUp({content = tostring(content), hideCancel = true})
end
local function stopCapture()
    send("cancel-key")
    if capturePopup then capturePopup:Hide(); capturePopup = nil end
end
local function showHeaderVersion(self, show)
    local version = self.view.applepieVersionText
    if show and not version then
        local title = self.view.tabTitleTxt
        local object = CSUtils.CreateObject(title.gameObject, title.rectTransform.parent)
        object.name = "ApplepieManagerVersion"
        version = object:GetComponent(title:GetType())
        -- Keep the game's title font/style, but let the label follow the header's
        -- right edge independently of its title text and the parent's layout.
        local layoutType = typeof(CS.UnityEngine.UI.LayoutElement)
        local layout = object:GetComponent(layoutType) or object:AddComponent(layoutType)
        layout.ignoreLayout = true
        local fitter = object:GetComponent(typeof(CS.UnityEngine.UI.ContentSizeFitter))
        if fitter then fitter.enabled = false end
        local rect = version.rectTransform
        rect.anchorMin = Vector2(0, rect.anchorMin.y)
        rect.anchorMax = Vector2(1, rect.anchorMax.y)
        rect.offsetMin = Vector2(0, rect.offsetMin.y)
        rect.offsetMax = Vector2(-24, rect.offsetMax.y)
        version.enableAutoSizing = false
        version.enableWordWrapping = false
        version.fontSize = title.fontSize * 0.6
        version.horizontalAlignment = CS.TMPro.HorizontalAlignmentOptions.Right
        version.raycastTarget = false
        version.text = "v0.2.1"
        self.view.applepieVersionText = version
    end
    if version then version.gameObject:SetActive(show) end
end
local function close(self)
    if active == self then
        stopCapture()
        showHeaderVersion(self, false)
        active, data = nil, nil
        send("close")
    end
end
local function button(label, value, click, enabled)
    rows[#rows + 1] = {kind = "button", label = label, value = value, click = click, enabled = enabled ~= false}
end
local function toggle(label, value, change, enabled)
    rows[#rows + 1] = {kind = "toggle", label = label, value = value, change = change, enabled = enabled ~= false}
end
local function dropdown(label, options, selected, change, enabled)
    rows[#rows + 1] = {kind = "dropdown", label = label, options = options, selected = selected, change = change, enabled = enabled ~= false}
end
local render
local function selectPage(index)
    stopCapture()
    page, data = index, nil
    send("open/" .. index)
    render(true)
end
local function editValue(plugin, field, revision)
    -- The shipped popup supports per-open characterLimit. Its next _ShowPopUp
    -- restores the game's default; no shared input delegate needs replacing.
    local instance = popup.AutoOpen(PanelId.CommonPopUp, nil, false)
    instance:_ShowPopUp({content = field.label, subContent = field.description,
        input = true, inputName = tostring(field.value), characterLimit = 255,
        onConfirm = function(value)
            if active and page == plugin then
                send("set/" .. plugin .. "/" .. revision .. "/" .. field.index .. "/" .. hex(value))
            end
        end})
end
local function buildRows()
    rows = {}
    if not data then
        button("Applepie Manager", text("正在读取…", "Loading…"), function() send("refresh") end)
        return
    end
    chinese = data.chinese
    if page == -1 then
        button(text("管理器设置", "Manager settings"), text("配置", "Configure"), function() selectPage(-2) end)
        for _, p in ipairs(data.plugins) do
            local index = p.index
            button(pluginLabel(p), text("配置", "Configure"), function() selectPage(index) end)
        end
        if #data.plugins == 0 then button(text("插件", "Plugins"), text("未发现插件", "No plugins"), function() end, false) end
    elseif page == -2 then
        button(text("返回插件列表", "Back to plugins"), text("返回", "Back"), function() selectPage(-1) end)
        dropdown(text("管理器语言", "Manager language"), {"简体中文", "English"}, chinese and 1 or 2,
            function(i) send("preference/language/" .. (i == 1 and "zh" or "en")) end)
        local labels = {
            {"hide_uid", "隐藏 UID", "Hide UID"}, {"hide_ping", "隐藏延迟", "Hide ping"},
            {"hide_menu_uid", "隐藏菜单 UID", "Hide menu UID"}, {"hide_menu_name", "隐藏菜单名称", "Hide menu name"},
            {"hide_card_uid", "隐藏名片 UID", "Hide card UID"}, {"hide_card_name", "隐藏名片名称", "Hide card name"}}
        for _, item in ipairs(labels) do
            local key = item[1]
            toggle(text(item[2], item[3]), data.preferences[key],
                function(value) send("preference/" .. key .. "/" .. (value and "1" or "0")) end)
        end
    else
        local p = data.plugin
        button(text("返回插件列表", "Back to plugins"), text("返回", "Back"), function() selectPage(-1) end)
        if not p then return end
        button(pluginLabel(p), text("详情", "Details"), function() notice(p.details) end)
        toggle(text("启用插件", "Enable plugin"), p.active,
            function(value) send("enable/" .. p.index .. "/" .. (value and "1" or "0")) end, p.canToggle)
        if p.config then
            local c = p.config
            for _, field in ipairs(c.fields) do
                local f, revision = field, c.revision
                local function set(value) send("set/" .. p.index .. "/" .. revision .. "/" .. f.index .. "/" .. hex(value)) end
                if f.type == 1 then
                    toggle(f.label, f.value == 1, function(value) set(value and "1" or "0") end, not c.pending)
                elseif f.type == 4 then
                    local selected = 1
                    for i, option in ipairs(f.options) do if option == f.value then selected = i end end
                    dropdown(f.label, f.options, selected, function(i) set(f.options[i]) end, not c.pending)
                elseif f.type == 6 then
                    button(f.label, f.display, function()
                        send("key/" .. p.index .. "/" .. revision .. "/" .. f.index)
                        capturePopup = popup.AutoOpen(PanelId.CommonPopUp, nil, false)
                        capturePopup:_ShowPopUp({content = text("请按下按键（Esc 取消）", "Press a key (Esc cancels)"),
                            onCancel = stopCapture})
                        -- Enter/Space are bindable keys, not popup confirmation.
                        local confirm = capturePopup.view.confirmButton.gameObject
                        confirm:SetActive(false)
                        restoreCaptureUI = function() confirm:SetActive(true); restoreCaptureUI = nil end
                    end, not c.pending)
                    button(f.label .. text(" · 清除", " · Clear"), text("清除绑定", "Clear binding"),
                        function() set("0") end, not c.pending and f.value ~= 0)
                else
                    button(f.label, tostring(f.value), function() editValue(p.index, f, revision) end, not c.pending)
                end
            end
            button(text("基础设置", "Basic settings"), c.pending and text("正在保存…", "Saving…") or text("修改后自动保存", "Changes save automatically"), function() end, false)
            if c.error ~= "" then button(text("保存失败", "Save failed"), c.error, function() notice(c.error) end) end
            button(text("恢复默认值", "Restore defaults"), text("重置", "Reset"), function()
                popup.ShowPopUp({content = text("恢复此插件的默认设置？", "Restore this plugin's defaults?"),
                    onConfirm = function() if active and page == p.index then send("defaults/" .. p.index .. "/" .. c.revision) end end})
            end, not c.pending)
        end
    end
    if data.message ~= "" then button(text("操作结果", "Result"), data.message, function() notice(data.message) end) end
    button(text("刷新状态", "Refresh status"), text("刷新", "Refresh"), function() send("refresh") end)
end
-- Current shipped GameSettingCtrl uses GameSettingItemCell widgets. Reuse their
-- templates, cache, state controller and navigation instead of the old beta
-- controller's _GetCachedItem / settingContent fields.
render = function(resetNavigation)
    if not active then return end
    local self = active
    buildRows()
    self.view.tabTitleTxt.text = "Applepie Manager"
    showHeaderVersion(self, true)
    self.view.loadNode.gameObject:SetActive(false)
    self.view.deviceNode.gameObject:SetActive(false)
    self.view.bottomNode.gameObject:SetActive(false)
    self.m_itemCellMap, self.m_qualitySubSettingItemCellMap = {}, {}
    self.m_contentHeight = 0
    local itemDataList, controls = {}, {}
    local types = GEnums.SettingItemType
    -- GameSettingItemCell hides cached controls by iterating this entire map.
    -- Keep native types such as Slider and Key even though our rows do not use them.
    for kind, config in pairs(self.m_itemControlConfigs) do controls[kind] = config end
    for _, kind in ipairs({types.Toggle, types.Dropdown, types.Button}) do
        local nativeConfig = assert(self.m_itemControlConfigs[kind], "Missing native setting control template")
        controls[kind] = {template = nativeConfig.template, initializer = function(cell)
            local row = cell.itemData.applepieRow
            local item = cell.itemControl
            if row.kind == "toggle" then
                item.toggle.view.toggle.checkIsValueValid = nil
                item.toggle:InitCommonToggle(function(value)
                    if active == self and row.enabled then row.change(value) end
                end, row.value, true, {text("开启", "On"), text("关闭", "Off")})
                item.toggle:ToggleInteractable(row.enabled)
            elseif row.kind == "dropdown" then
                item.dropdown:ClearComponent()
                item.dropdown.onToggleOptList:RemoveAllListeners()
                item.dropdown.onValidateSelectCell = nil
                item.dropdown:Init(function(csIndex, option)
                    option:SetText(row.options[LuaIndex(csIndex)])
                    option:SetState("Normal")
                end, function(csIndex)
                    if active == self and row.enabled then row.change(LuaIndex(csIndex)) end
                end)
                item.dropdown.onToggleOptList:AddListener(function(open)
                    if open then self:_DropdownAdjustExpandDirection(cell); item.dropdown:ScrollToSelected() end
                end)
                item.dropdown:Refresh(#row.options, CSIndex(row.selected), false)
                item.dropdown.interactable = row.enabled
            else
                item.buttonText.text = row.value
                item.buttonIcon.gameObject:SetActive(true)
                item.stateCtrl:SetState("NormalState")
                item.button.onClick:RemoveAllListeners()
                item.button.onClick:AddListener(function()
                    if active == self and row.enabled then row.click() end
                end)
                item.button.interactable = row.enabled
            end
        end}
    end
    self.m_itemDataList[self.m_tabIndex] = itemDataList
    self.m_itemCells:Refresh(#rows, function(cell, index)
        local row = rows[index]
        local kind = row.kind == "toggle" and types.Toggle or (row.kind == "dropdown" and types.Dropdown or types.Button)
        local id = "applepie_row_" .. index
        local itemData = {settingId = id, settingText = row.label, settingItemType = kind,
            settingGroupTitle = "", settingRedDot = "", applepieRow = row}
        itemDataList[index] = itemData
        cell:InitGameSettingItemCell(itemData, controls, {})
        self.m_itemCellMap[id] = cell
        local height = self.m_itemCellHeightWithoutTitle
        UIUtils.setSizeDeltaY(cell.rectTransform, height)
        if index > 1 then self.m_contentHeight = self.m_contentHeight + self.view.config.SETTING_ITEM_VERTICAL_SPACE end
        self.m_contentHeight = self.m_contentHeight + height
        local pos = cell.rectTransform.anchoredPosition
        cell.rectTransform.anchoredPosition = Vector2(pos.x, -self.m_contentHeight)
    end)
    UIUtils.setSizeDeltaY(self.view.viewContent, self.m_contentHeight)
    if resetNavigation then
        self:_SetSettingItemControllerNaviTarget()
        self.view.scrollView:ScrollTo(Vector2(0, 0), true)
    end
end

-- HyperLua supports reloading an existing type; preserve all original methods
-- and use the same signatures as the game's GameSettingCtrl.
local reloading = HL.IsReloading()
local function addTab(self)
    if not self.m_tabDataList or not self.m_itemDataList then return false end
    for _, tab in ipairs(self.m_tabDataList) do if tab.tabId == tabId then return false end end
    local icon = self.m_tabDataList[1] and self.m_tabDataList[1].tabIcon or "general"
    table.insert(self.m_tabDataList, {tabId = tabId, tabText = "Applepie Manager", tabIcon = icon,
        tabRedDot = "", tabLeaveFunction = "", tabItems = {}})
    self.m_itemDataList[#self.m_tabDataList] = {}
    return true
end
HL.SetReloading(true)
local registered, registerError = xpcall(function()
ctrl = HL.Class("GameSettingCtrl", baseModule.UICtrl)
ctrl._BuildSettingDataList = HL.Method() << function(self)
    oldBuild(self)
    addTab(self)
end
ctrl._RefreshSettingTabCell = HL.Method(HL.Table, HL.Number) << function(self, cell, index)
    oldTabCell(self, cell, index)
    local tab = self.m_tabDataList[index]
    if tab and tab.tabId == tabId then
        local sprite = tabSprite()
        if sprite then
            cell.defaultIcon.sprite = sprite
            cell.selectedIcon.sprite = sprite
            cell.defaultIcon.preserveAspect = true
            cell.selectedIcon.preserveAspect = true
        end
    end
end
ctrl._RefreshSettingTab = HL.Method(HL.Number, HL.Boolean, HL.Opt(HL.Boolean)) << function(self, index, init, rebuildData)
    local tab = self.m_tabDataList[index]
    if tab and tab.tabId == tabId then
        self.m_tabIndex = index
        if active ~= self or init then
            active = self
            selectPage(-1)
        else
            render()
        end
    else
        close(self)
        oldTab(self, index, init, rebuildData)
    end
end
ctrl.OnHide = HL.Override() << function(self)
    close(self)
    if oldHide then oldHide(self) end
end
ctrl.OnShow = HL.Override() << function(self)
    if oldShow then oldShow(self) end
    if addTab(self) then self:_InitSettingTabList() end
    if not self.m_tabCells then return end
    for i, tab in ipairs(self.m_tabDataList) do
        if tab.tabId == tabId then
            local cell = self.m_tabCells:Get(i)
            if cell and cell.toggle.isOn then self.m_tabIndex = i; active = self; selectPage(-1) end
        end
    end
end
ctrl.OnClose = HL.Override() << function(self)
    close(self)
    if oldClose then oldClose(self) end
end
HL.Commit(ctrl)
-- CommonPopUp is shared by the game: restore its confirmation button after key
-- capture on every close path and before the next popup.
popup = HL.Class("CommonPopUpCtrl", baseModule.UICtrl)
popup._ShowPopUp = HL.Method(HL.Table) << function(self, args)
    if restoreCaptureUI then restoreCaptureUI() end
    oldPopupShow(self, args)
end
popup.OnHide = HL.Override() << function(self)
    if restoreCaptureUI then restoreCaptureUI() end
    oldPopupHide(self)
end
popup.OnClose = HL.Override() << function(self)
    if restoreCaptureUI then restoreCaptureUI() end
    oldPopupClose(self)
end
HL.Commit(popup)
end, debug.traceback)
HL.SetReloading(reloading)
if not registered then error(registerError) end
_G.ApplepieNativeSettings = {
    OnEvent = function(snapshot)
        if not active or snapshot.page ~= page then return end
        if snapshot.keyFinished and capturePopup then capturePopup:Hide(); capturePopup = nil end
        data = snapshot
        -- Text popup editing remains intact when a background operation finishes.
        render()
    end
}
local opened, existing = UIManager:IsOpen(PanelId.GameSetting)
if opened and existing.isFinishedCreation and addTab(existing) then existing:_InitSettingTabList() end
send("ready")
return "ready"
