











set_project("gpxinput")
set_version("2.0.0")

set_allowedmodes("debug", "release")
set_defaultmode("release")
set_languages("c++17")


set_runtimes("MD")

add_rules("mode.debug", "mode.release")
add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "UNICODE", "_UNICODE")
add_cxxflags("/EHsc", "/W3", "/utf-8")
add_includedirs("include")

if is_mode("release") then
    set_optimize("fastest")
else
    set_optimize("none")
    set_symbols("debug")
end




target("gen_def")
    set_kind("binary")
    add_files("tools/gen_def/gen_def.cpp")
    set_targetdir("$(builddir)/tools")








local proxy_sources = {
    "proxy/gp_proxy.cpp",
    "proxy/gp_real.cpp",
    "proxy/gp_hooks.cpp",
    "proxy/gp_engine.cpp",
    "proxy/gp_ipc_client.cpp",
    "proxy/gp_hid.cpp",
    "proxy/gp_wgi.cpp",
    "proxy/gp_wgihook.cpp",
    "proxy/gp_capture.cpp",
    "proxy/gp_haptics.cpp",
    "proxy/gp_gamestate.cpp",
    "proxy/gp_config.cpp",
    "proxy/gp_log.cpp",
}

local function add_proxy(name, def_file)
    target(name)
        set_kind("shared")
        set_filename(name .. ".dll")
        set_basename(name)





        set_policy("build.optimization.lto", true)

        add_files(proxy_sources)
        add_files(def_file)

        add_includedirs("proxy", "minhook/include")
        add_linkdirs("minhook/lib")
        add_links("libMinHook.x64")


        add_syslinks("setupapi", "hid", "user32", "runtimeobject")


        set_targetdir("$(builddir)/proxy/" .. name)

        if is_mode("release") then

            set_symbols("hidden")
        end
    target_end()
end

add_proxy("xinput1_4",   "proxy/exports_xinput1_4.def")
add_proxy("xinput1_3",   "proxy/exports_xinput1_3.def")
add_proxy("xinput9_1_0", "proxy/exports_xinput9_1_0.def")




target("gp_processor")
    set_kind("binary")
    set_filename("gp_processor.exe")
    add_files("processor/processor_main.cpp")
    add_files("processor/gp_ini.cpp")


    add_files("processor/gp_plugin_default.cpp")
    add_includedirs("processor")
    set_targetdir("$(builddir)/bin")


target("gp_plugin_default")
    set_kind("shared")
    set_filename("gp_plugin_default.dll")
    add_files("processor/gp_plugin_default.cpp")


    add_defines("GP_PLUGIN_BUILD_DLL")
    set_targetdir("$(builddir)/bin")




target("xinput_test")
    set_kind("binary")
    add_files("tools/xinput_test/xinput_test.cpp")
    set_targetdir("$(builddir)/tools")








target("gpxinput_hook")
    set_kind("shared")
    set_filename("gpxinput_hook.dll")
    set_basename("gpxinput_hook")
    add_files(proxy_sources)
    add_includedirs("proxy", "minhook/include")
    add_linkdirs("minhook/lib")
    add_links("libMinHook.x64")
    add_syslinks("setupapi", "hid", "user32", "runtimeobject")
    set_targetdir("$(builddir)/bin")
    if is_mode("release") then
        set_symbols("hidden")
    end




target("gpxinput_injector")
    set_kind("binary")
    set_filename("gpxinput_injector.exe")
    add_files("injector/injector_main.cpp")
    set_targetdir("$(builddir)/bin")






target("gp_vibtest")
    set_kind("binary")
    add_files("tools/gp_vibtest/gp_vibtest.cpp")
    add_syslinks("setupapi", "hid")
    set_targetdir("$(builddir)/tools")








target("gp_trigtest")
    set_kind("binary")
    add_files("tools/gp_trigtest/gp_trigtest.cpp")
    add_includedirs("C:/Program Files (x86)/Windows Kits/10/Include/10.0.18362.0/cppwinrt")
    add_syslinks("windowsapp")
    set_targetdir("$(builddir)/tools")





target("gp_state_view")
    set_kind("binary")
    add_files("tools/gp_state_view/gp_state_view.cpp")
    add_includedirs("include")
    set_targetdir("$(builddir)/tools")






target("hgi_probe")
    set_kind("binary")
    add_files("tools/hgi_probe/hgi_probe.cpp")
    add_syslinks("runtimeobject")
    set_targetdir("$(builddir)/tools")






target("shresolve_test")
    set_kind("binary")
    add_files("tools/shresolve_test/shresolve_test.cpp")
    add_includedirs("asi_rdr2")
    set_targetdir("$(builddir)/tools")






target("gp_envtest")
    set_kind("binary")
    add_files("tools/gp_envtest/gp_envtest.cpp")
    add_files("proxy/gp_haptics.cpp")
    add_files("proxy/gp_config.cpp")
    add_files("proxy/gp_log.cpp")
    add_includedirs("proxy", "include")
    set_targetdir("$(builddir)/tools")






target("gp_monitor")
    set_kind("binary")
    add_files("tools/gp_monitor/gp_monitor.cpp")


    set_targetdir("$(builddir)/bin")













target("gpxinput_rdr2")
    set_kind("shared")
    set_filename("gpxinput_rdr2.asi")
    add_files("asi_rdr2/gpxinput_rdr2.cpp")
    add_includedirs("asi_rdr2")
    set_targetdir("$(builddir)/bin")






target("plugin_test")
    set_kind("binary")
    add_files("tools/plugin_test/plugin_test.cpp")
    add_files("processor/gp_plugin_default.cpp")
    set_targetdir("$(builddir)/tools")














target("gp_protosniff")
    set_kind("binary")
    add_files("tools/gp_protosniff/gp_protosniff.cpp")
    add_files("tools/gp_protosniff/gpsniff_decode.cpp")
    add_syslinks("setupapi", "hid")
    set_targetdir("$(builddir)/tools")
















target("gp_vibsend")
    set_kind("binary")
    add_files("tools/gp_vibsend/gp_vibsend.cpp")
    add_syslinks("setupapi", "hid", "runtimeobject")
    set_targetdir("$(builddir)/tools")















local padlab_sources = {
    "tools/padlab/padlab.cpp",
    "tools/padlab/padlab_xi.cpp",
}

local function add_padlab(name, main_src, family_src, variant, links)
    target(name)
        set_kind("binary")
        set_filename(name .. ".exe")
        add_files(main_src)
        add_files(padlab_sources)
        add_includedirs("tools/padlab")
        add_syslinks("setupapi", "hid")
        if links then
            for _, l in ipairs(links) do add_syslinks(l) end
        end
        if variant then
            add_defines("PADLAB_VARIANT=" .. variant)
        end
        set_targetdir("$(builddir)/tools/padlab")
    target_end()
end


add_padlab("padlab_xbox360",    "tools/padlab/xbox_family.cpp", nil, 1, {"runtimeobject"})
add_padlab("padlab_xboxone",    "tools/padlab/xbox_family.cpp", nil, 2, {"runtimeobject"})
add_padlab("padlab_xboxseries", "tools/padlab/xbox_family.cpp", nil, 3, {"runtimeobject"})
add_padlab("padlab_xboxelite",  "tools/padlab/xbox_family.cpp", nil, 4, {"runtimeobject"})


add_padlab("padlab_xbox_licensed", "tools/padlab/xbox_licensed.cpp", nil, nil, {"runtimeobject"})


add_padlab("padlab_ds3",       "tools/padlab/sony_family.cpp", nil, 1, nil)
add_padlab("padlab_ds4",       "tools/padlab/sony_family.cpp", nil, 2, nil)
add_padlab("padlab_dualsense", "tools/padlab/sony_family.cpp", nil, 3, nil)


add_padlab("padlab_ns_procon", "tools/padlab/ns_family.cpp", nil, 1, nil)
add_padlab("padlab_ns_joycon", "tools/padlab/ns_family.cpp", nil, 2, nil)
