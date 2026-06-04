#include <reshade.hpp>
#include <imgui.h>
#include <windows.h>
#include <string>

// وضعیت نمایش منو (شروع پیش‌فرض: مخفی)
static bool show_var_menu = false;
static bool key_was_down = false;

// متغیرهای کنترل خط آفساید و زوایای دوربین
static float offside_line_x = 0.0f;
static float offside_line_y = 0.0f;
static float camera_rotation = 0.0f;
static bool enable_lines = true;

// هندل‌های متغیرهای شیدر ریشید
static reshade::api::effect_variable var_line_pos_variable = { 0 };
static reshade::api::effect_variable var_enable_variable = { 0 };

// تابع جستجوی متغیرها در شیدر OffsidePlane.fx
static void find_shader_variables(reshade::api::effect_runtime *runtime)
{
    runtime->enumerate_uniform_variables(nullptr, [](reshade::api::effect_runtime *runtime, reshade::api::effect_variable variable) {
        char name[64] = "";
        size_t size = sizeof(name);
        
        // حل مشکل ارور C2664 با پاس دادن آدرس size (&size)
        runtime->get_uniform_variable_name(variable, name, &size); 

        std::string var_name(name);
        if (var_name == "offside_line_position" || var_name == "u_line_pos") {
            var_line_pos_variable = variable;
        }
        if (var_name == "enable_offside_line" || var_name == "u_enable") {
            var_enable_variable = variable;
        }
    });
}

// این تابع در هر فریم بازی اجرا می‌شود
static void on_reshade_present(reshade::api::effect_runtime *runtime)
{
    // ۱. بررسی فشردن کلید N (کد اسکی 0x4E) بدون تداخل و تکرار سریع
    bool key_is_down = (GetAsyncKeyState(0x4E) & 0x8000) != 0;
    if (key_is_down && !key_was_down)
    {
        show_var_menu = !show_var_menu; // سوئیچ کردن وضعیت منو
    }
    key_was_down = key_is_down;

    // ۲. رندر کردن منو در صورت فعال بودن (کاملاً مستقل از منوی اصلی ریشید)
    if (show_var_menu)
    {
        // در صورتی که هندل متغیرها هنوز گرفته نشده، آن‌ها را پیدا کن
        if (var_line_pos_variable.handle == 0) {
            find_shader_variables(runtime);
        }

        // تنظیم اندازه پیش‌فرض منو هنگام اولین باز شدن
        ImGui::SetNextWindowSize(ImVec2(450, 320), ImGuiCond_FirstUseEver);
        
        // زیباسازی منو (تم سبز استادیومی و گوشه‌های گرد)
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.11f, 0.47f, 0.24f, 1.0f)); 
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.15f, 0.68f, 0.37f, 1.0f));

        if (ImGui::Begin("⚽ VAR Offside System (Standalone)", &show_var_menu, ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.4f, 1.0f), "PES 2021 - Interactive VAR Control Panel");
            ImGui::Separator();
            ImGui::Spacing();

            // فعال یا غیرفعال کردن کل خطوط
            if (ImGui::Checkbox("Enable VAR Offside Lines", &enable_lines))
            {
                if (var_enable_variable.handle != 0) {
                    uint32_t val = enable_lines ? 1 : 0;
                    runtime->set_uniform_value_uint(var_enable_variable, &val, 1);
                }
            }

            ImGui::Spacing();
            ImGui::Text("Line Position Adjustments:");
            
            // اسلایدر جابجایی خط افقی روی زمین
            if (ImGui::SliderFloat("Line X Position", &offside_line_x, -50.0f, 50.0f, "%.3f"))
            {
                if (var_line_pos_variable.handle != 0) {
                    runtime->set_uniform_value_float(var_line_pos_variable, &offside_line_x, 1);
                }
            }

            // اسلایدر جابجایی عمودی (در صورت نیاز به توسعه در آینده)
            ImGui::SliderFloat("Line Y Position", &offside_line_y, -50.0f, 50.0f, "%.3f");

            // اسلایدر تنظیم زاویه تصحیح دوربین
            ImGui::SliderFloat("Camera Angle Fix", &camera_rotation, -180.0f, 180.0f, "%.2f °");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // دکمه ریست کردن تنظیمات
            if (ImGui::Button("Reset All to Default", ImVec2(-1, 30)))
            {
                offside_line_x = 0.0f;
                offside_line_y = 0.0f;
                camera_rotation = 0.0f;
                enable_lines = true;
                if (var_line_pos_variable.handle != 0) runtime->set_uniform_value_float(var_line_pos_variable, &offside_line_x, 1);
                if (var_enable_variable.handle != 0) { uint32_t val = 1; runtime->set_uniform_value_uint(var_enable_variable, &val, 1); }
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tip: Press 'N' anytime in-game to toggle this menu.");
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }
}

extern "C" __declspec(dllexport) bool DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule))
            return false;
        // ثبت رویداد پرزنت برای رندر دائم و مستقل منو
        reshade::register_event<reshade::addon_event::reshade_present>(on_reshade_present);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_event<reshade::addon_event::reshade_present>(on_reshade_present);
        reshade::unregister_addon(hModule);
        break;
    }
    return true;
}

extern "C" __declspec(dllexport) const char *ReShadeAddonName = "VAR_Offside_System";
extern "C" __declspec(dllexport) const char *ReShadeAddonDescription = "Standalone Hotkey Menu for PES 2021 VAR System";
