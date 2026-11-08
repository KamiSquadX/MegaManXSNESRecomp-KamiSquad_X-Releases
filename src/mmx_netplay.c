/* MMX's session rules. Transport, lobby discovery and rollback stay shared. */
#include "mmx_options.h"
#include "mmx_netplay.h"
#include "mmx_boss_rush_foreign.h"
#include "mod_runtime.h"
#include "mmx_coop.h"
#include "mmx_weapons.h"
#include "mmx_armor.h"
#include <stdio.h>
#include <string.h>

#if defined(RECOMP_LAUNCHER) && !MMX_VARIANT_JP
#include "recomp_launcher.h"

static const char *const kCoop = "megaman-x.coop";
static const char *const kCoopAxl = "megaman-x.coop-axl";
static const char *const kZero = "megaman-x.character.zero";
static const char *const kAxl = "megaman-x.character.axl";
static const char *const kTeam = "megaman-x.team-select";
static const char *const kWide = "megaman-x.enhancement.widescreen";
static const RecompLauncherCModProvider *s_mods;
static const RecompLauncherCNetplayCallbacks *s_net;
static RecompLauncherCModProvider s_provider;
static RecompLauncherCNetplayCallbacks s_callbacks;
static int s_active;
static char s_error[512];

int MmxNetplayActive(void) { return s_active; }

/* Either co-op mod satisfies netplay: X + Zero (0.0.1) or X / Zero / Axl
 * (0.0.2). They claim the same plugins, so at most one is ever enabled. */
static int is_coop(const char *pkg) { return !strcmp(pkg, kCoop) || !strcmp(pkg, kCoopAxl); }
/* Solo character modes claim the co-op plugins; enabling one would turn
 * co-op off, so netplay keeps them off and out of the list. */
static int is_solo(const char *pkg) {
  return !strcmp(pkg, kZero) || !strcmp(pkg, kAxl) || !strcmp(pkg, kTeam);
}
static const char *active_coop(void) {
  if (snes_mod_runtime_feature_enabled_c(kCoopAxl, "coop")) return kCoopAxl;
  if (snes_mod_runtime_feature_enabled_c(kCoop, "coop")) return kCoop;
  return NULL;
}

int MmxNetplayReady(char *reason, size_t cap) {
  if(snes_mod_runtime_feature_enabled_c("megaman-x.armor","armor") && !MmxArmorEnabled()) {
    snprintf(reason,cap,"The selected X armor could not load. Check its source ROM and cache directory.");
    return 0;
  }
  if (!MmxCoopEnabled()) {
    snprintf(reason, cap, "X + Zero co-op could not load. Check the selected X3 ROM and cache directory.");
    return 0;
  }
  for (unsigned page = 1; page <= 2; ++page) {
    const char *id = page == 1 ? "megaman-x.weapons.x2" : "megaman-x.weapons.x3";
    if (snes_mod_runtime_feature_enabled_c(id, "weapons") && !MmxWeaponsPageEnabled(page)) {
      snprintf(reason, cap, "The selected X%u weapon pack could not load.", page + 1);
      return 0;
    }
  }
  for (unsigned game = 2; game <= 3; ++game) {
    const char *id = game == 2 ? "megaman-x.boss-rush.x2-bosses" : "megaman-x.boss-rush.x3-bosses";
    if (snes_mod_runtime_feature_enabled_c(id, "bosses") && !MmxBossRushForeignReady(game)) {
      snprintf(reason, cap, "The selected X%u Boss Rush Mavericks could not load.", game);
      return 0;
    }
  }
  return 1;
}

static int fail(const char *message) {
  snprintf(s_error, sizeof(s_error), "%s", message);
  return 0;
}
static const char *mod_error(void *ctx) {
  return s_error[0] ? s_error : s_mods->last_error(ctx);
}
static const char *net_error(void *ctx) {
  return s_error[0] ? s_error : s_net->last_error ? s_net->last_error(ctx) : "";
}
static void clear_error(void *ctx) {
  s_error[0] = 0;
  if (s_net->clear_last_error) s_net->clear_last_error(ctx);
}

static void mode_changed(int enabled) {
  s_error[0] = 0;
  if (!enabled) {
    snes_mod_runtime_end_temporary_c();
    s_active = 0;
    return;
  }
  if (s_active) return;
  s_mods = snes_mod_runtime_launcher_provider_c();
  if (!s_mods || !snes_mod_runtime_begin_temporary_c()) {
    fail("Netplay requires the bundled X + Zero co-op mod.");
    return;
  }
  s_active = 1;
  s_mods->feature_enable(s_mods->ctx, kZero, "zero", 0);
  s_mods->feature_enable(s_mods->ctx, kAxl, "axl", 0);
  s_mods->feature_enable(s_mods->ctx, kTeam, "team", 0);
  /* Keep the co-op mod the player already chose; otherwise Co-op 0.0.2
   * (X / Zero / Axl), falling back to 0.0.1 only when it is missing. */
  if (!active_coop() && !s_mods->feature_enable(s_mods->ctx, kCoopAxl, "coop", 1) &&
      !s_mods->feature_enable(s_mods->ctx, kCoop, "coop", 1))
    fail("The co-op mods are missing. Restore the bundled mods.");
  /* The offline layout is not part of an online room. This temporary plan
   * restores the player's saved local preference when they leave Netplay. */
  s_mods->feature_set_option(s_mods->ctx, kCoop, "coop", "local_cameras", "unified");
  s_mods->feature_set_option(s_mods->ctx, kCoopAxl, "coop", "local_cameras", "unified");
  /* Preserve an existing fixed choice; Adaptive becomes 16:9 in this
   * temporary plan only. Disabled widescreen remains the native view. */
  char aspect[32] = {0};
  snes_mod_runtime_feature_option_value_c(kWide, "widescreen", "aspect", aspect, sizeof(aspect));
  if (!strcmp(aspect, "adaptive"))
    s_mods->feature_set_option(s_mods->ctx, kWide, "widescreen", "aspect", "16:9");
}

static int validate_plan(void) {
  const char *coop = s_active ? active_coop() : NULL;
  if (!coop)
    return fail("Netplay requires a co-op mod. Enable X + Zero Co-op or X / Zero / Axl Co-op.");
  if (snes_mod_runtime_feature_enabled_c(kZero, "zero"))
    return fail("Character switching must be off during co-op netplay.");
  char cameras[32] = {0};
  snes_mod_runtime_feature_option_value_c(coop, "coop", "cameras", cameras, sizeof(cameras));
  if (strcmp(cameras, "independent") && strcmp(cameras, "unified"))
    return fail("Choose Independent or Unified netplay cameras. Restore the bundled co-op mod if this option is missing.");
  if (snes_mod_runtime_feature_enabled_c(kWide, "widescreen")) {
    char aspect[32] = {0};
    snes_mod_runtime_feature_option_value_c(kWide, "widescreen", "aspect", aspect, sizeof(aspect));
    if (strcmp(aspect, "16:9") && strcmp(aspect, "21:9") && strcmp(aspect, "32:9"))
      return fail("Choose a fixed netplay view: 16:9, 21:9, or 32:9.");
  }
  s_error[0] = 0;
  return 1;
}

/* Co-op camera settings (0.0.2's "views") is offered only while Co-op 0.0.2
 * is ticked; its Select teleport only with split screens or Independent
 * netplay cameras. */
static int coop_axl_on(void) { return snes_mod_runtime_feature_enabled_c(kCoopAxl, "coop"); }
static int views_feature(const char *pkg, const char *feature) {
  return !strcmp(pkg, kCoopAxl) && !strcmp(feature, "views");
}
static int teleport_allowed(void) {
  char value[32] = {0};
  snes_mod_runtime_feature_option_value_c(kCoopAxl, "coop", s_active ? "cameras" : "local_cameras",
                                          value, sizeof(value));
  return !strcmp(value, s_active ? "independent" : "split");
}
static int feature_enable(void *ctx, const char *pkg, const char *feature, int enabled) {
  s_error[0] = 0;
  if (enabled && views_feature(pkg, feature) && !coop_axl_on())
    return fail("Tick Co-op 0.0.2 (X / Zero / Axl) first.");
  if (s_active && is_solo(pkg) && enabled)
    return fail("Netplay always uses co-op; Add Zero, Add Axl and Solo Team select are unavailable.");
  if (s_active && is_coop(pkg) && !enabled) {
    /* Unticking X / Zero / Axl Co-op goes back to X + Zero Co-op; the room
     * always has one co-op mod. */
    if (!strcmp(pkg, kCoopAxl) && s_mods->feature_enable(ctx, kCoop, "coop", 1)) return 1;
    return fail("Netplay requires a co-op mod. Tick the other co-op mod to switch.");
  }
  return s_mods->feature_enable(ctx, pkg, feature, enabled);
}
static int set_enabled(void *ctx, const char *pkg, int enabled) {
  if (s_active && ((is_coop(pkg) && !enabled && active_coop() && !strcmp(active_coop(), pkg)) ||
                   (is_solo(pkg) && enabled)))
    return fail("Netplay requires a co-op mod.");
  s_error[0] = 0;
  return s_mods->set_enabled(ctx, pkg, enabled);
}
static int hidden_camera_option(const char *id) {
  return !strcmp(id, s_active ? "local_cameras" : "cameras");
}
static int feature_get(void *ctx, int index, RecompLauncherCModFeature *out) {
  if (!s_mods->feature_get(ctx, index, out)) return 0;
  if (is_coop(out->package_id)) {
    RecompLauncherCModOption option;
    for (int i=0; s_mods->feature_option_get(ctx, out->package_id, out->id, i, &option); ++i)
      if (hidden_camera_option(option.id)) --out->option_count;
  }
  if (s_active && is_solo(out->package_id)) out->hidden = 1;
  if (views_feature(out->package_id, out->id) && !coop_axl_on()) {
    out->locked = 1;
    snprintf(out->status, sizeof(out->status), "Tick Co-op 0.0.2 first");
  }
  if (s_active && is_coop(out->package_id) && !out->has_error)
    snprintf(out->status, sizeof(out->status), "Netplay uses one co-op mod");
  return 1;
}
static int option_get(void *ctx, const char *pkg, const char *fid, int index,
                      RecompLauncherCModOption *out) {
  if (is_coop(pkg)) {
    int visible=0, found=0;
    for (int i=0; s_mods->feature_option_get(ctx, pkg, fid, i, out); ++i) {
      if (hidden_camera_option(out->id)) continue;
      if (visible++==index) { found=1; break; }
    }
    if (!found) return 0;
  } else if (!s_mods->feature_option_get(ctx, pkg, fid, index, out)) return 0;
  if (views_feature(pkg, fid) && !strcmp(out->id, "select_teleport") && !teleport_allowed())
    out->disabled = 1;
  if (s_active && !strcmp(pkg, kWide) && !strcmp(out->id, "aspect")) {
    out->choice_count = 3;
    snprintf(out->default_value, sizeof(out->default_value), "16:9");
    snprintf(out->description, sizeof(out->description),
        "The host chooses the same fixed view for both players. Resizing scales the picture. Netplay uses original CRT pixel proportions.");
  }
  return 1;
}
static int choice_get(void *ctx, const char *pkg, const char *fid, const char *option,
                      int index, RecompLauncherCModChoice *out) {
  if (s_active && !strcmp(pkg, kWide) && !strcmp(option, "aspect")) {
    static const char *const ratios[] = {"16:9", "21:9", "32:9"};
    if (index < 0 || index >= 3) return 0;
    snprintf(out->value, sizeof(out->value), "%s", ratios[index]);
    snprintf(out->label, sizeof(out->label), "%s", ratios[index]);
    return 1;
  }
  return s_mods->feature_choice_get(ctx, pkg, fid, option, index, out);
}
static int set_option(void *ctx, const char *pkg, const char *fid, const char *option, const char *value) {
  s_error[0] = 0;
  if (views_feature(pkg, fid) && !strcmp(option, "select_teleport") && !strcmp(value, "true") &&
      !teleport_allowed())
    return fail(s_active ? "Select teleport needs Netplay cameras: Independent (Experimental)."
                         : "Select teleport needs Local co-op display: Side by side (Experimental).");
  if (!s_active && is_coop(pkg) && !strcmp(option, "cameras"))
    return fail("Choose Local co-op display for offline play. Open Netplay to choose the room's cameras.");
  if (s_active && is_coop(pkg) && !strcmp(option, "local_cameras"))
    return fail("Local co-op display is available offline. Choose Netplay cameras for the room.");
  if (!strcmp(option, "behavior") && !strcmp(pkg, kCoopAxl) && !strcmp(fid, "coop")) {
    /* 0.0.2's Zero behavior follows the same shared preference. */
    if (!s_mods->feature_set_option(ctx, pkg, fid, option, value)) return 0;
    (void)s_mods->feature_set_option(ctx, kZero, "zero", option, value);
    (void)s_mods->feature_set_option(ctx, kCoop, "coop", option, value);
    return 1;
  }
  if (!strcmp(option, "behavior") &&
      ((!strcmp(pkg, kZero) && !strcmp(fid, "zero")) ||
       (!strcmp(pkg, kCoop) && !strcmp(fid, "coop")))) {
    /* One user preference exposed in both mutually exclusive character
     * features. Keep both catalog values identical so whichever feature is
     * active also carries this rule in saves and the netplay agreement.
     * Netplay's temporary plan provides its existing restore-on-exit scope. */
    const char *other = !strcmp(pkg, kZero) ? kCoop : kZero;
    const char *other_fid = !strcmp(pkg, kZero) ? "coop" : "zero";
    char previous[32] = {0};
    snes_mod_runtime_feature_option_value_c(pkg, fid, option, previous, sizeof(previous));
    if (!s_mods->feature_set_option(ctx, pkg, fid, option, value)) return 0;
    if (!s_mods->feature_set_option(ctx, other, other_fid, option, value)) {
      s_mods->feature_set_option(ctx, pkg, fid, option, previous);
      return fail("Cannot update the shared Zero behavior. Restore both bundled character mods.");
    }
    (void)s_mods->feature_set_option(ctx, kCoopAxl, "coop", option, value);
    return 1;
  }
  if (s_active && !strcmp(pkg, kWide) && !strcmp(option, "aspect") &&
      strcmp(value, "16:9") && strcmp(value, "21:9") && strcmp(value, "32:9"))
    return fail("Adaptive view is available offline. Choose a fixed ratio for netplay.");
  return s_mods->feature_set_option(ctx, pkg, fid, option, value);
}
static int commit_netplay(void *ctx, const char *rom) {
  return validate_plan() && s_mods->commit(ctx, rom);
}
static int no(void *ctx) { (void)ctx; return 0; }
static int create(void *ctx, const char *name, char *endpoint, const char *password,
                   const RecompLauncherCSettings *settings, int lan, int slots) {
  (void)slots;
  mode_changed(1);
  if (!validate_plan()) return -1;
  if (s_net->allow_spectators_set) s_net->allow_spectators_set(ctx, 0);
  return s_net->create(ctx, name, endpoint, password, settings, lan, 2);
}
static int fill_launch(void *ctx, RecompLauncherCNetplayLaunch *out) {
  if (!s_net->fill_launch(ctx, out)) return 0;
  if (out->is_spectator || out->host_spectates || out->max_slots != 2 || out->player_count != 2)
    return fail("Mega Man X netplay requires exactly two players.");
  return 1;
}

void MmxNetplayConfigureLauncher(RecompLauncherCGameInfo *info) {
  if (!info->netplay_supported || !info->netplay || !info->mods) return;
  s_mods = info->mods;
  s_provider = *s_mods;
  s_provider.feature_enable = feature_enable;
  s_provider.set_enabled = set_enabled;
  s_provider.feature_get = feature_get;
  s_provider.feature_option_get = option_get;
  s_provider.feature_choice_get = choice_get;
  s_provider.feature_set_option = set_option;
  s_provider.commit_netplay = commit_netplay;
  s_provider.last_error = mod_error;
  info->mods = &s_provider;
  info->netplay_mode_changed = mode_changed;
  s_net = info->netplay;
  s_callbacks = *s_net;
  s_callbacks.create = create;
  s_callbacks.fill_launch = fill_launch;
  s_callbacks.last_error = net_error;
  s_callbacks.clear_last_error = clear_error;
  s_callbacks.allow_spectators_get = NULL;
  s_callbacks.allow_spectators_set = NULL;
  s_callbacks.host_can_spectate = no;
  s_callbacks.automatch_available = no; /* Co-op has no competitive queue. */
  info->netplay = &s_callbacks;
}

int MmxNetplayPrepare(int from_lobby, char *reason, size_t cap) {
  /* Every peer starts from X1's dash behavior, whatever its own file says. */
  MmxOptionsUseDefaults();
  if (!from_lobby) mode_changed(1);
  if (validate_plan()) return 1;
  snprintf(reason, cap, "%s", s_error);
  return 0;
}
#else
int MmxNetplayActive(void) { return 0; }
int MmxNetplayReady(char *reason, size_t cap) { return MmxNetplayPrepare(0, reason, cap); }
void MmxNetplayConfigureLauncher(struct RecompLauncherCGameInfo *info) { (void)info; }
int MmxNetplayPrepare(int from_lobby, char *reason, size_t cap) {
  (void)from_lobby;
  snprintf(reason, cap, "Netplay requires the USA co-op build.");
  return 0;
}
#endif
