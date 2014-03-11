enum dualboot_system {
	INVALID_SYSTEM = -1,
	SYSTEM1,
	SYSTEM2
};

void dualboot_init(void);
void dualboot_show_selection_ui(void);
enum dualboot_system get_selected_system(void);
void dualboot_setup_env(void);
void dualboot_set_system(enum dualboot_system sys);
