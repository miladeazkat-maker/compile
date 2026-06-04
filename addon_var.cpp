#include <windows.h>
#include <psapi.h>
#include <reshade.hpp>
#include <imgui.h>
#include <string>

// تعاریف پایه‌ای حافظه PES 2021
const uintptr_t BASE_OFFSET = 0x037F89E0;
const uintptr_t NEW_BASE_OFFSET = 0x036F0260;

uintptr_t cam0_height_addr = 0;
uintptr_t zoom_addr = 0;
bool is_menu_open = false;

// متغیرهای هماهنگ‌کننده منو و شیدر
int current_view_idx = 0; // 0: View 1, 1: View 2, 2: View 3, 3: View 4, 4: Custom
float plane_position = 0.5f;
bool enable_plane = true;

// هندل‌های متغیرهای یونيفورم شیدر ریشید
reshade::api::effect_uniform_variable uniform_uCameraView = { 0 };
reshade::api::effect_uniform_variable uniform_uTargetDepth = { 0 };
reshade::api::effect_uniform_variable uniform_uShowPlane = { 0 };

// تابع امن نوشتن در حافظه (تغییر سطح دسترسی به PAGE_EXECUTE_READWRITE)
bool WriteMemory(uintptr_t address, const void* data, size_t size) {
    if (!address) return false;
    DWORD oldProtect;
    if (VirtualProtect(reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy(reinterpret_cast<LPVOID>(address), data, size);
        VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &oldProtect);
        return true;
    }
    return false;
}

// اسکنر باینری برای پیدا کردن پترن‌های کاملا اختصاصی دوربین در حافظه متنی بازی
uintptr_t FindPattern(const char* pattern, const char* mask) {
    MODULEINFO modInfo = { 0 };
    HMODULE hModule = GetModuleHandle(NULL);
    if (!hModule) return 0;
    GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO));
    
    uintptr_t base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    size_t size = modInfo.SizeOfImage;
    size_t patternLength = strlen(mask);

    for (size_t i = 0; i < size - patternLength; i++) {
        bool found = true;
        for (size_t j = 0; j < patternLength; j++) {
            if (mask[j] != '?' && pattern[j] != *reinterpret_cast<char*>(base + i + j)) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return 0;
}

// حل کردن زنجیره پوینترهای آدرس هدف اول
uintptr_t GetTargetAddress() {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    uintptr_t ptr = *reinterpret_cast<uintptr_t*>(base + BASE_OFFSET);
    if (!ptr) return 0;
    ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x138); if (!ptr) return 0;
    ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x20);  if (!ptr) return 0;
    ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x8);   if (!ptr) return 0;
    return ptr + 0xC;
}

// حل کردن زنجیره پوینترهای آدرس هدف دوم
uintptr_t GetNewPointerAddress() {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    uintptr_t ptr = *reinterpret_cast<uintptr_t*>(base + NEW_BASE_OFFSET);
    if (!ptr) return 0;
    ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x138); if (!ptr) return 0;
    ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x20);  if (!ptr) return 0;
    ptr = *reinterpret_cast<uintptr_t*>(ptr + 0x8);   if (!ptr) return 0;
    return ptr + 0x10;
}

// اعمال تغییرات مستقیم روی فیزیک و زوایای دوربین بازی
void ApplyGameViewMemory(float rot, float new_ptr, float height, float zoom) {
    uintptr_t addr1 = GetTargetAddress();
    if (addr1) WriteMemory(addr1, &rot, sizeof(float));

    uintptr_t addr2 = GetNewPointerAddress();
    if (addr2) WriteMemory(addr2, &new_ptr, sizeof(float));

    if (cam0_height_addr) WriteMemory(cam0_height_addr, &height, sizeof(float));
    if (zoom_addr) WriteMemory(zoom_addr, &zoom, sizeof(float));
}

// همگام‌سازی متغیرهای داخلی منو با ران‌تایم شیدر ریشید
void SyncUniformsWithReshade(reshade::api::effect_runtime* runtime) {
    if (uniform_uCameraView.handle) runtime->set_uniform_value_int(uniform_uCameraView, current_view_idx);
    if (uniform_uTargetDepth.handle) runtime->set_uniform_value_float(uniform_uTargetDepth, plane_position);
    if (uniform_uShowPlane.handle) runtime->set_uniform_value_bool(uniform_uShowPlane, enable_plane);
}

// جستجو و یافتن متغیرهای Uniform داخل فایل .fx
void LocateShaderUniforms(reshade::api::effect_runtime* runtime) {
    runtime->enumerate_uniform_variables(nullptr, [](reshade::api::effect_runtime* rt, reshade::api::effect_uniform_variable variable) {
        char name[64] = "";
        rt->get_uniform_variable_name(variable, name, sizeof(name));
        if (strcmp(name, "uCameraView") == 0) uniform_uCameraView = variable;
        else if (strcmp(name, "uTargetDepth") == 0) uniform_uTargetDepth = variable;
        else if (strcmp(name, "uShowPlane") == 0) uniform_uShowPlane = variable;
    });
}

// هوکِ بخش رندر منو (ImGui Custom Draw)
static void OnDrawOverlay(reshade::api::effect_runtime* runtime) {
    // باز و بسته شدن با کلید F4 روی کیبورد
    if (GetAsyncKeyState(VK_F4) & 1) {
        is_menu_open = !is_menu_open;
    }

    if (!is_menu_open) return;

    // استایل‌دهی شیک و گیمینگ به منوی اختصاصی
    ImGui::SetNextWindowSize(ImVec2(400, 320), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.12f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.40f, 0.20f, 1.00f)); // تم سبز فوتبالی برای هدر

    if (ImGui::Begin("⚽ SAOT & VAR CENTRAL CONTROL PANEL", &is_menu_open, ImGuiWindowFlags_NoCollapse)) {
        LocateShaderUniforms(runtime);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "SYSTEM STATUS: ACTIVE");
        ImGui::Separator();

        // سوییچ فعال/غیرفعال‌سازی کل سیستم
        if (ImGui::Checkbox("Enable Offside Plane Overlay", &enable_plane)) {
            SyncUniformsWithReshade(runtime);
        }

        ImGui::Spacing();
        ImGui::Text("Select Broadcasting View:");
        
        // رادیو باتن‌های انتخاب نما
        const char* views[] = { "Camera View 1", "Camera View 2", "Camera View 3", "Camera View 4" };
        bool view_changed = false;
        for (int i = 0; i < 4; i++) {
            if (ImGui::RadioButton(views[i], &current_view_idx, i)) {
                view_changed = true;
            }
            if (i == 1 || i == 3) ImGui::SameLine(); // چینش دو در دو دکمه‌ها
        }

        // اعمال آنی دیتای معماری دوربین در صورت تغییر نما توسط کاربر
        if (view_changed) {
            if (current_view_idx == 0)      ApplyGameViewMemory(0.687f, 7.5f, 1.70f, 1.00f);
            else if (current_view_idx == 1) ApplyGameViewMemory(2.675f, 7.5f, 1.70f, 1.00f);
            else if (current_view_idx == 2) ApplyGameViewMemory(-0.515f, 7.5f, 1.70f, 1.00f);
            else if (current_view_idx == 3) ApplyGameViewMemory(-2.823f, 7.5f, 1.70f, 1.00f);
            SyncUniformsWithReshade(runtime);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // اسلایدر فوق‌العاده نرم برای تغییر آنی لوکیشن خط آفساید
        ImGui::Text("Fine-Tune Offside Line Position:");
        if (ImGui::SliderFloat("##plane_pos", &plane_position, 0.001f, 0.1f, "Position: %.5f")) {
            SyncUniformsWithReshade(runtime);
        }

        ImGui::Spacing();
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 35);
        ImGui::TextDisabled("Press F4 to Hide/Show this Control Panel.");
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// نقطه ورود (Entrypoint) افزونه به ریشید هنگام لود شدن بازی
extern "C" __declspec(dllexport) const char* GetAddonName() { return "PES 2021 Semi-Automated Offside Core"; }
extern "C" __declspec(dllexport) const char* GetAddonDescription() { return "Unified Master UI linking Sider Engine and ReShade Render Layer."; }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule)) return FALSE;
        
        // اجرای پترن اسکن‌های سنگین در زمان بالا آمدن اولیه افزونه برای کش کردن آدرس‌ها
        cam0_height_addr = FindPattern("\xc7\x45\xab\xcd\xcc\xcc\x3e\xf3\x41\x0f\x58\xc0\xf3\x0f\x11\x45\xa7\x0f\x28\xc6", "xxxxxxxxxxxxxxxxxxxx");
        if (cam0_height_addr) cam0_height_addr += 3;

        zoom_addr = FindPattern("\xc7\x46\x30\xc3\xf5\x28\x3f\xf3\x44\x0f\x10\x06\xf3\x44\x0f\x10\x4e\x08\xf3\x44\x0f\x11\x45\xc7", "xxxxxxxxxxxxxxxxxxxxxxxx");
        if (zoom_addr) zoom_addr += 3;

        reshade::register_overlay(nullptr, OnDrawOverlay);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_overlay(nullptr, OnDrawOverlay);
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}