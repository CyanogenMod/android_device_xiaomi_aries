/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/input.h>

#include "dualboot.h"
#include "recovery_ui.h"
#include "common.h"
#include "extendedcommands.h"

char* MENU_HEADERS[] = { NULL };

char* MENU_ITEMS[] = { "Managed System",
                       "reboot system now",
                       "install zip",
                       "wipe data/factory reset",
                       "wipe cache partition",
                       "backup and restore",
                       "mounts and storage",
                       "advanced",
                       NULL };

static void update_menu_items(void) {
	int sys = get_selected_system();

	if(sys==SYSTEM1)
		MENU_ITEMS[0] = "Managed system [System1]";
	else if(sys==SYSTEM2)
		MENU_ITEMS[0] = "Managed system [System2]";
	else
		MENU_ITEMS[0] = "Managed system [Invalid]";
}

void device_ui_init(UIParameters* ui_parameters) {
}

int device_recovery_start() {
    dualboot_init();
    update_menu_items();
    return 0;
}

int device_reboot_now(volatile char* key_pressed, int key_code) {
    return 0;
}

int device_perform_action(int which) {
	if(which==0) {
		dualboot_show_selection_ui();
		update_menu_items();
		dualboot_setup_env();
	}

    return which-1;
}

int device_wipe_data() {
    return 0;
}

int device_verify_root_and_recovery(void) {
	dualboot_set_system(SYSTEM1);
	verify_root_and_recovery();
	dualboot_set_system(SYSTEM2);
	verify_root_and_recovery();

	return 0;
}

int device_build_selection_title(char* buf, const char* title) {
	enum dualboot_system sys = get_selected_system();
	char* prefix = "?";

	if(sys==SYSTEM1)
		prefix = "System1";
	else if(sys==SYSTEM2)
		prefix = "System2";

	sprintf(buf, "[%s] %s", prefix, title);
	return 0;
}
