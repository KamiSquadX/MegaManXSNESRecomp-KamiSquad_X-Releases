/* SNES first-launch input defaults, in launcher_binds.c.
 *
 * Two reports this pins:
 *
 *  1. "when a game is first launched it shows Gamepad in the drop down,
 *     instead of the name of the gamepad it defaulted to". config.ini says
 *     EnableGamepad1, so the slot's source is Gamepad, but nothing has ever
 *     named a device -- no GUID -- and the Input source box fell back to the
 *     generic placeholder while the game was about to use a real controller.
 *     Hydration now binds the first live pad no other slot has claimed.
 *
 *  2. The deadzone that comes with it. A pad with no saved profile gets the
 *     console default (40%), not whatever the previous selection left in the
 *     slot: a deadzone belongs to the device.
 *
 * Also: the SNES face-button chip labels (named by SNES position, so the
 * default layout reads A/B/X/Y beside A/B/X/Y), and the named input profiles
 * of the Configure page (save, load, rename, delete, order, persistence).
 *
 * Includes the translation unit under test (as launcher_discs_test.c does) so
 * the static profile helpers are reachable, and links launcher_model.c for the
 * real set_source/visible_player_count. Every other console bridge is stubbed.
 */
#include "launcher_binds.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Generated stubs. launcher_binds.c reaches every console's bridge; this
 * test drives only the SNES paths, so the rest resolve to empty bodies. */

/* A small in-memory keyboard store, so profiles can round-trip key maps. */
static SDL_Scancode g_keys[2][20];
int recompui_keybinds_button_count(void) { return 20; }
SDL_Scancode recompui_keybinds_get_button(int player, int button) {
    return (player == 1 || player == 2) && button >= 0 && button < 20 ? g_keys[player - 1][button] : 0;
}
void recompui_keybinds_init(const char *exe_path) { (void)exe_path; }
void recompui_keybinds_reset_player(int player) { (void)player; }
void recompui_keybinds_save(void) { }
void recompui_keybinds_set_button(int player, int button, SDL_Scancode sc) {
    if ((player == 1 || player == 2) && button >= 0 && button < 20) g_keys[player - 1][button] = sc;
}
int rui_gb_binds_get(const char* path, int b) { (void)path; (void)b; return 0; }
int rui_gb_binds_get_slot(const char* path, int b, int slot) { (void)path; (void)b; (void)slot; return 0; }
void rui_gb_binds_init(const char* path) { (void)path; }
void rui_gb_binds_reset(const char* path) { (void)path; }
void rui_gb_binds_set(const char* path, int b, int scancode) { (void)path; (void)b; (void)scancode; }
void rui_gb_binds_set_slot(const char* path, int b, int slot, int scancode) { (void)path; (void)b; (void)slot; (void)scancode; }
int rui_genesis_binds_get_key(const char* path, int player, int b) { (void)path; (void)player; (void)b; return 0; }
void rui_genesis_binds_get_pad(const char* path, int player, int b, int* kind, int* code, int* axis_dir) { (void)path; (void)player; (void)b; (void)kind; (void)code; (void)axis_dir; }
void rui_genesis_binds_init(const char* path) { (void)path; }
void rui_genesis_binds_reset(const char* path, int player) { (void)path; (void)player; }
void rui_genesis_binds_set_key(const char* path, int player, int b, int scancode) { (void)path; (void)player; (void)b; (void)scancode; }
void rui_genesis_binds_set_pad(const char* path, int player, int b, int kind, int code, int axis_dir) { (void)path; (void)player; (void)b; (void)kind; (void)code; (void)axis_dir; }
void rui_n64_binds_get(const char* path, int device, int b, int slot, int* out_type, int* out_id) { (void)path; (void)device; (void)b; (void)slot; (void)out_type; (void)out_id; }
void rui_n64_binds_init(const char* path) { (void)path; }
void rui_n64_binds_label(int type, int id, char* out, size_t cap) { (void)type; (void)id; (void)out; (void)cap; }
void rui_n64_binds_reset_device(const char* path, int device) { (void)path; (void)device; }
void rui_n64_binds_set(const char* path, int device, int b, int slot, int type, int id) { (void)path; (void)device; (void)b; (void)slot; (void)type; (void)id; }
void rui_n64_pad_binds_init(const char* path) { (void)path; }
void rui_n64_pad_binds_path(const char* bind_cfg_path, char* out, int cap) { (void)bind_cfg_path; (void)out; (void)cap; }
void rui_n64_pad_binds_remember(const char* path, const char* guid, const char* name, int deadzone_pct) { (void)path; (void)guid; (void)name; (void)deadzone_pct; }
void rui_n64_pad_binds_save_profile(const char* path, const char* guid, const char* name, int name_custom, int deadzone_pct) { (void)path; (void)guid; (void)name; (void)name_custom; (void)deadzone_pct; }
void rui_n64_pad_binds_rename(const char* path, const char* guid, const char* name) { (void)path; (void)guid; (void)name; }
void rui_n64_pad_binds_delete(const char* path, const char* guid) { (void)path; (void)guid; }
int rui_n64_pad_binds_name_is_custom(const char* path, const char* guid) { (void)path; (void)guid; return 0; }
void rui_n64_pad_binds_reset(const char* path, const char* guid) { (void)path; (void)guid; }
void rui_n64_pad_binds_set(const char* path, const char* guid, int b, int kind, int code, int axis_dir) { (void)path; (void)guid; (void)b; (void)kind; (void)code; (void)axis_dir; }
void rui_n64_pad_binds_source(const char* path, const char* guid, int b, char* out, int cap) { (void)path; (void)guid; (void)b; (void)out; (void)cap; }
int rui_nes_binds_get(const char* path, int player, int b) { (void)path; (void)player; (void)b; return 0; }
void rui_nes_binds_init(const char* path) { (void)path; }
void rui_nes_binds_reset(const char* path, int player) { (void)path; (void)player; }
void rui_nes_binds_set(const char* path, int player, int b, int scancode) { (void)path; (void)player; (void)b; (void)scancode; }
int rui_nes_camera_bind_get(const char* path, int action) { (void)path; (void)action; return 0; }
void rui_nes_camera_bind_reset(const char* path) { (void)path; }
void rui_nes_camera_bind_set(const char* path, int action, int scancode) { (void)path; (void)action; (void)scancode; }
void rui_nes_zapper_get(const char* path, int* mouse_enabled, int* crosshair) { (void)path; (void)mouse_enabled; (void)crosshair; }
void rui_nes_zapper_set(const char* path, int mouse_enabled, int crosshair) { (void)path; (void)mouse_enabled; (void)crosshair; }
void rui_pad_binds_sibling_path(const char* ref, const char* file, char* out, int cap) { (void)ref; (void)file; (void)out; (void)cap; }
int rui_psx_binds_get_slot(const char* path, int player, int b, int slot) { (void)path; (void)player; (void)b; (void)slot; return 0; }
void rui_psx_binds_init(const char* path) { (void)path; }
void rui_psx_binds_reset(const char* path, int player) { (void)path; (void)player; }
void rui_psx_binds_save(const char* path) { (void)path; }
void rui_psx_binds_set_slot(const char* path, int player, int b, int slot, int scancode) { (void)path; (void)player; (void)b; (void)slot; (void)scancode; }
int rui_psx_pad_binds_deadzone(const char* path, const char* guid) { (void)path; (void)guid; return 0; }
void rui_psx_pad_binds_delete(const char* path, const char* guid) { (void)path; (void)guid; }
void rui_psx_pad_binds_init(const char* path) { (void)path; }
int rui_psx_pad_binds_known_at(const char* path, int index, char* guid, int guid_cap, char* name, int name_cap) { (void)path; (void)index; (void)guid; (void)guid_cap; (void)name; (void)name_cap; return 0; }
int rui_psx_pad_binds_known_count(const char* path) { (void)path; return 0; }
void rui_psx_pad_binds_label(const char* path, const char* guid, int b, char* out, int cap) { (void)path; (void)guid; (void)b; (void)out; (void)cap; }
void rui_psx_pad_binds_name(const char* path, const char* guid, char* out, int cap) { (void)path; (void)guid; (void)out; (void)cap; }
int rui_psx_pad_binds_name_is_custom(const char* path, const char* guid) { (void)path; (void)guid; return 0; }
void rui_psx_pad_binds_remember(const char* path, const char* guid, const char* name, int deadzone_pct) { (void)path; (void)guid; (void)name; (void)deadzone_pct; }
void rui_psx_pad_binds_rename(const char* path, const char* guid, const char* name) { (void)path; (void)guid; (void)name; }
void rui_psx_pad_binds_reset(const char* path, const char* guid) { (void)path; (void)guid; }
void rui_psx_pad_binds_save_profile(const char* path, const char* guid, const char* name, int name_custom, int deadzone_pct) { (void)path; (void)guid; (void)name; (void)name_custom; (void)deadzone_pct; }
void rui_psx_pad_binds_set(const char* path, const char* guid, int b, int kind, int code, int axis_dir) { (void)path; (void)guid; (void)b; (void)kind; (void)code; (void)axis_dir; }

static int fails;

static void expect(int cond, const char* what) {
    if (cond) { printf("ok: %s\n", what); return; }
    fprintf(stderr, "FAIL: %s\n", what);
    ++fails;
}

static SystemProfile g_snes_prof;

static void make_model(LauncherModel* m) {
    memset(m, 0, sizeof(*m));
    g_snes_prof = kSystemProfileSnes;
    m->profile = &g_snes_prof;
    m->player_count = 2;
    /* What config.ini's EnableGamepad1/2 produce: both slots are "gamepad",
     * neither names a device. */
    m->s.player_src[0] = 2;
    m->s.player_src[1] = 2;
    m->s.deadzone[0] = 30;   /* the value the former default converted to */
    m->s.deadzone[1] = 30;
}

int main(int argc, char** argv) {
    /* Keep the profile store out of any real config.ini. */
    static char cfg[1024];
    snprintf(cfg, sizeof(cfg), "%s/snes_pad_defaults_test.ini",
             argc > 1 ? argv[1] : ".");
    remove(cfg);
    g_launcher_config_path = cfg;

    LauncherPad pads[2];
    memset(pads, 0, sizeof(pads));
    snprintf(pads[0].guid, sizeof(pads[0].guid), "%s", "03000000aabbccdd0001");
    snprintf(pads[0].name, sizeof(pads[0].name), "%s", "DualSense Wireless Controller");
    pads[0].id = 7;
    snprintf(pads[1].guid, sizeof(pads[1].guid), "%s", "03000000aabbccdd0002");
    snprintf(pads[1].name, sizeof(pads[1].name), "%s", "8BitDo SN30 Pro");
    pads[1].id = 9;

    /* ---- one live pad, two gamepad slots ------------------------------- */
    LauncherModel m;
    make_model(&m);
    launcher_binds_hydrate_snes_pad_names(&m, pads, 1);

    expect(strcmp(m.s.player_gamepad_guid[0], pads[0].guid) == 0,
           "player 1 binds the live pad's GUID");
    expect(strcmp(m.player_pad_name[0], pads[0].name) == 0,
           "and shows its name, not the generic placeholder");
    expect(strcmp(launcher_model_player_src_label(&m, 0), pads[0].name) == 0,
           "so the Input source label reads the device name");
    expect(m.player_pad_id[0] == pads[0].id, "and carries the SDL id");
    expect(m.s.deadzone[0] == RUI_SNES_PAD_DEFAULT_DEADZONE_PCT,
           "an unconfigured pad takes the console default deadzone");
    expect(m.s.player_gamepad_guid[1][0] == '\0',
           "player 2 does not steal the one pad player 1 holds");

    /* ---- a second pad appears ------------------------------------------ */
    launcher_binds_hydrate_snes_pad_names(&m, pads, 2);
    expect(strcmp(m.s.player_gamepad_guid[1], pads[1].guid) == 0,
           "player 2 binds the second pad once it is live");
    expect(strcmp(m.s.player_gamepad_guid[0], pads[0].guid) == 0,
           "and player 1 keeps the pad it already had");

    /* ---- a saved profile wins over the default ------------------------- */
    make_model(&m);
    {
        char sect[96];
        snes_profile_section(pads[0].guid, sect, sizeof(sect));
        launcher_ini_kv_write(cfg, sect, "Deadzone", "22");
        launcher_ini_kv_write(cfg, sect, "Name", "Alex's pad");
    }
    launcher_binds_hydrate_snes_pad_names(&m, pads, 1);
    expect(m.s.deadzone[0] == 22, "a saved profile's deadzone wins over the default");
    expect(strcmp(m.player_pad_name[0], "Alex's pad") == 0,
           "and so does its saved name");

    /* ---- hydration never re-binds a slot that already has a GUID ------- */
    m.s.deadzone[0] = 3;                       /* as if dragged on the slider */
    launcher_binds_hydrate_snes_pad_names(&m, pads, 2);
    expect(m.s.deadzone[0] == 3,
           "a per-frame hydration does not fight a live slider edit");

    /* ---- the slider's setter clamps ------------------------------------ */
    launcher_model_set_deadzone(&m, 0, 137);
    expect(m.s.deadzone[0] == 100, "set_deadzone clamps above 100");
    launcher_model_set_deadzone(&m, 0, -4);
    expect(m.s.deadzone[0] == 0, "set_deadzone clamps below 0");
    launcher_model_set_deadzone(&m, 0, 7);
    expect(m.s.deadzone[0] == 7, "and takes any whole percent between");

    /* ---- face-button chip labels -------------------------------------- */
    {
        char out[32];
        snes_gamepad_label("B", out, sizeof(out));
        expect(strcmp(out, "A") == 0, "the right face button (default SNES A) reads A");
        snes_gamepad_label("A", out, sizeof(out));
        expect(strcmp(out, "B") == 0, "the bottom face button (default SNES B) reads B");
        snes_gamepad_label("Y", out, sizeof(out));
        expect(strcmp(out, "X") == 0, "the top face button (default SNES X) reads X");
        snes_gamepad_label("X", out, sizeof(out));
        expect(strcmp(out, "Y") == 0, "the left face button (default SNES Y) reads Y");
        snes_gamepad_label("Lb", out, sizeof(out));
        expect(strcmp(out, "L1") == 0, "other labels are unchanged");
        expect(RUI_SNES_PAD_DEFAULT_DEADZONE_PCT == 40, "the SNES default deadzone is 40%");
    }

    /* ---- named input profiles ------------------------------------------ */
    {
        char profiles[1100];
        const char* slash = strrchr(cfg, '/');
        snprintf(profiles, sizeof(profiles), "%.*sinput_profiles.ini",
                 slash ? (int)(slash - cfg + 1) : 0, cfg);
        remove(profiles);
        s_named_loaded = 0;
        expect(launcher_binds_input_profile_count() == 0, "no profiles before the first save");

        make_model(&m);
        launcher_model_set_source(&m, 0, 2, 7, "DualSense", "03000000aabbccdd0001");
        m.s.deadzone[0] = 25;
        launcher_ini_kv_write(cfg, "GamepadMap", "Controls",
            "DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, A, B, X, Y, Lb, Rb");
        g_keys[0][0] = (SDL_Scancode)4;
        expect(launcher_binds_input_profile_save(&m, 1, "  Main  ") == 0,
               "a new profile goes to index 0, name trimmed");
        expect(strcmp(launcher_binds_input_profile_name(0), "Main") == 0, "its name is stored");

        m.s.deadzone[0] = 33;
        launcher_ini_kv_write(cfg, "GamepadMap", "Controls",
            "DpadUp, DpadDown, DpadLeft, DpadRight, Back, Start, B, A, Y, X, Lb, Rb");
        g_keys[0][0] = (SDL_Scancode)5;
        expect(launcher_binds_input_profile_save(&m, 1, "Alt") == 1, "the next profile goes last");
        expect(launcher_binds_input_profile_count() == 2, "two profiles listed");
        expect(launcher_binds_input_profile_find("main") == 0, "names match without case");
        expect(launcher_binds_input_profile_save(&m, 1, "ALT") == 1,
               "saving under an existing name replaces it in place");
        expect(launcher_binds_input_profile_count() == 2, "and adds nothing");

        /* Load "Main" into player 1 with no controller connected. */
        launcher_model_set_source(&m, 0, 1, 0, NULL, NULL);
        m.s.deadzone[0] = 3;
        g_keys[0][0] = (SDL_Scancode)9;
        expect(launcher_binds_input_profile_load(&m, 1, 0, NULL, 0),
               "loading a profile succeeds");
        expect(m.s.player_src[0] == 2 &&
               strcmp(m.s.player_gamepad_guid[0], "03000000aabbccdd0001") == 0,
               "it switches to the saved controller, connected or not");
        expect(strcmp(m.player_pad_name[0], "DualSense") == 0, "and names it");
        expect(m.player_pad_id[0] == 0 && m.s.player_gamepad_instance[0] == 0,
               "a disconnected controller has no SDL instance");
        expect(m.s.deadzone[0] == 25, "its deadzone comes back");
        expect(g_keys[0][0] == 4, "its keyboard map comes back");
        {
            char controls[LNG_SNES_PAD_BUTTON_COUNT][48];
            snes_gamepad_read_config_order(0, controls);
            expect(strcmp(controls[6], "A") == 0 && strcmp(controls[7], "B") == 0,
                   "its gamepad map is written back to [GamepadMap]");
        }

        /* A live controller resolves to its SDL id. */
        expect(launcher_binds_input_profile_load(&m, 1, 0, pads, 2) &&
               m.player_pad_id[0] == pads[0].id, "a connected controller gets its live id");

        expect(!launcher_binds_input_profile_rename(1, "main"),
               "renaming onto another profile's name is refused");
        expect(launcher_binds_input_profile_rename(1, "Boss fights"), "rename succeeds");
        expect(launcher_binds_input_profile_delete(0), "delete succeeds");
        expect(launcher_binds_input_profile_count() == 1 &&
               strcmp(launcher_binds_input_profile_name(0), "Boss fights") == 0,
               "the remaining profile moves up");

        s_named_loaded = 0;   /* as on the next launch */
        expect(launcher_binds_input_profile_count() == 1 &&
               strcmp(launcher_binds_input_profile_name(0), "Boss fights") == 0,
               "profiles persist in input_profiles.ini");

        /* A keyboard profile switches the slot back to the keyboard. */
        launcher_model_set_source(&m, 1, 1, 0, NULL, NULL);
        g_keys[1][3] = (SDL_Scancode)44;
        expect(launcher_binds_input_profile_save(&m, 2, "Keys") == 1, "a keyboard profile saves");
        launcher_model_set_source(&m, 1, 2, 9, pads[1].name, pads[1].guid);
        g_keys[1][3] = 0;
        expect(launcher_binds_input_profile_load(&m, 2, 1, pads, 2) &&
               m.s.player_src[1] == 1 && !m.s.player_gamepad_guid[1][0] && g_keys[1][3] == 44,
               "loading it switches to Keyboard and restores the keys");
        remove(profiles);
    }

    remove(cfg);
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("snes_pad_defaults_test: ok");
    return 0;
}
