#include "Config.hpp"
#include "Config/Config.hpp"
#include "Gui/imgui_impl_sdl2.h"
#include "Gui/Ui.hpp"
#include "utils.h"

#include <SDL.h>
#include <SDL_image.h>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <ostream>
#include <thread>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <filesystem>

#include "Emulator.hpp"
#include "Logger.hpp"
#include "Data/EventCode.hpp"
#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_mouse.h"
#include "SDL_video.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <csignal>

static bool abort_flag = false;

using namespace casioemu;
// #define DEBUG
int main(int argc, char *argv[])
{
	std::map<std::string, std::string> argv_map;
	for (int ix = 1; ix != argc; ++ix)
	{
		std::string key, value;
		char *eq_pos = strchr(argv[ix], '=');
		if (eq_pos)
		{
			key = std::string(argv[ix], eq_pos);
			value = eq_pos + 1;
		}
		else
		{
			key = "model";
			value = argv[ix];
		}

		if (argv_map.find(key) == argv_map.end())
			argv_map[key] = value;
		else
			logger::Info("[argv] #%i: key '%s' already set\n", ix, key.c_str());
	}

	if (argv_map.find("model") == argv_map.end())
	{

#ifdef DEBUG
		argv_map["model"]="E:/projects/CasioEmuX/models/fx991cncw";
#else
		argv_map["model"]=EmuGloConfig.GetModulePath();
#endif
		// printf("No model path supplied\n");
		// exit(2);
	}

	int sdlFlags = SDL_INIT_VIDEO | SDL_INIT_TIMER;
	if (SDL_Init(sdlFlags) != 0)
		PANIC("SDL_Init failed: %s\n", SDL_GetError());

	int imgFlags = IMG_INIT_PNG;
	if (IMG_Init(imgFlags) != imgFlags)
		PANIC("IMG_Init failed: %s\n", IMG_GetError());

	std::string history_filename;
	auto history_filename_iter = argv_map.find("history");
	if (history_filename_iter != argv_map.end())
		history_filename = history_filename_iter->second;

	if (!history_filename.empty())
	{
		
	}

    for (auto s: {SIGTERM, SIGINT}) {
        signal(s, [](int) {
            abort_flag = true;
        });
    }
	// while(1)
	// 	;
	{
		Emulator emulator(argv_map);
		
		// Note: argv_map must be destructed after emulator.

        // start colored spans file watcher thread
        std::thread t1([&] {
            auto colored_spans_file = emulator.GetModelFilePath("mem-spans.txt");

            auto last_mtime = 0L;
            while (true) {
                if (std::filesystem::exists(colored_spans_file)) {
                    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::filesystem::last_write_time(colored_spans_file).time_since_epoch()
                    ).count();

                    if (timestamp != last_mtime) {
                        // update data
                        DebugUi::UpdateMarkedSpans(casioemu::parseColoredSpansConfig(colored_spans_file));
                        last_mtime = timestamp;
                    }
                } else {
                    DebugUi::UpdateMarkedSpans({});
                }
                sleep(1 /* 1s */);
            }
        });
        t1.detach();

		// Used to signal to the console input thread when to stop.
		static std::atomic<bool> running(true);

		// std::thread console_input_thread([&] {
		// 	struct terminate_thread {};

		// 	while (1)
		// 	{
		// 		char *console_input_c_str;

		// 		if (console_input_c_str == NULL)
		// 		{
		// 			if(argv_map.find("exit_on_console_shutdown") != argv_map.end())
		// 			{
		// 				SDL_Event event;
		// 				SDL_zero(event);
		// 				event.type = SDL_WINDOWEVENT;
		// 				event.window.event = SDL_WINDOWEVENT_CLOSE;
		// 				SDL_PushEvent(&event);
		// 			}
		// 			else
		// 			{
		// 				logger::Info("Console thread shutting down\n");
		// 			}

		// 			break;
		// 		}

		// 		// Ignore empty lines.
		// 		if (console_input_c_str[0] == 0)
		// 			continue;


		// 		std::lock_guard<decltype(emulator.access_mx)> access_lock(emulator.access_mx);
		// 		if (!emulator.Running())
		// 			break;
		// 		emulator.ExecuteCommand(console_input_c_str);
		// 		free(console_input_c_str);

		// 		if (!emulator.Running())
		// 		{
		// 			SDL_Event event;
		// 			SDL_zero(event);
		// 			event.type = SDL_USEREVENT;
		// 			event.user.code = CE_EMU_STOPPED;
		// 			SDL_PushEvent(&event);
		// 			return;
		// 		}
		// 	}
		// });
		DebugUi ui(&emulator);
		// std::thread t1([&](){
		// 	while(1){
		// 		gui_loop();
		// 	}
		// });
		// t1.detach();
		while (emulator.Running())
		{
			
			//std::cout<<SDL_GetMouseFocus()<<","<<emulator.window<<std::endl;
			SDL_Event event;
			ui.PaintUi();
			if (!SDL_PollEvent(&event))
				continue;

            if (abort_flag) {
                abort_flag = false;
                SDL_Event ev_exit;
                SDL_zero(ev_exit);
                ev_exit.type = SDL_WINDOWEVENT;
                ev_exit.window.event = SDL_WINDOWEVENT_CLOSE;
                SDL_PushEvent(&ev_exit);
            }

			switch (event.type)
			{
			case SDL_USEREVENT:
				switch (event.user.code)
				{
				case CE_FRAME_REQUEST:
					emulator.Frame();
					break;
				case CE_EMU_STOPPED:
					if (emulator.Running())
						PANIC("CE_EMU_STOPPED event received while emulator is still running\n");
					break;
				}
				break;

			case SDL_WINDOWEVENT:
				
				switch (event.window.event)
				{
				case SDL_WINDOWEVENT_CLOSE:
					emulator.Shutdown();
					break;
				case SDL_WINDOWEVENT_RESIZED:
					 if (!emulator.IsResizable())
					 {
					 	// Normally, in this case, the window manager should not
					 	// send resized event, but some still does (such as xmonad)
					 	break;
					 }
					ImGui_ImplSDL2_ProcessEvent(&event);
					if(event.window.windowID == SDL_GetWindowID(emulator.window)){
					emulator.WindowResize(event.window.data1, event.window.data2);
					}
					break;
				case SDL_WINDOWEVENT_EXPOSED:
					emulator.Repaint();
					break;
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
			case SDL_KEYDOWN:
			case SDL_KEYUP:
			case SDL_TEXTINPUT:
			case SDL_MOUSEMOTION:
			case SDL_MOUSEWHEEL:
				if(SDL_GetKeyboardFocus()!=emulator.window && SDL_GetMouseFocus()!=emulator.window)
				{
					ImGui_ImplSDL2_ProcessEvent(&event);
					break;
				}
				emulator.UIEvent(event);
				break;
			}
		}
		
		running = false;
		//console_input_thread.join();
	}

	std::cout << '\n';
	
	IMG_Quit();
	SDL_Quit();

	if (!history_filename.empty())
	{
		
	}

	return 0;
}
