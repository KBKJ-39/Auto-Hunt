//=============================================================
// Auto-Hunt System for rAthena — Core Implementation
// Version: 3.0
// See autohunt.hpp for version history
//=============================================================

#include "../common/showmsg.hpp"
#include "../common/malloc.hpp"
#include "../common/nullpo.hpp"
#include "../common/timer.hpp"
#include "../common/utils.hpp"
#include "../common/strlib.hpp"

#include "atcommand.hpp"
#include "battle.hpp"
#include "clif.hpp"
#include "itemdb.hpp"
#include "map.hpp"
#include "mob.hpp"
#include "path.hpp"
#include "pc.hpp"
#include "skill.hpp"
#include "status.hpp"
#include "unit.hpp"

#include "autohunt.hpp"

// Global instance
AutoHuntManager autohunt;

// Timer interval in milliseconds
#define AUTOHUNT_INTERVAL 500

// Maximum search range
#define AUTOHUNT_MAX_RANGE 150

// Stuck detection threshold (ticks before teleporting)
#define AUTOHUNT_STUCK_THRESHOLD 6

/*===========
 * Auto-Hunt Manager Constructor/Destructor
 *------------------------------------------*/
AutoHuntManager::AutoHuntManager() : initialized(false) {
}

AutoHuntManager::~AutoHuntManager() {
	finalize();
}

/*===========
 * Initialize Auto-Hunt system
 *------------------------------------------*/
void AutoHuntManager::init(void) {
	if (initialized) {
		return;
	}

	add_timer_func_list(autohunt_timer, "autohunt_timer");
	initialized = true;

	ShowStatus("Auto-Hunt system initialized.\n");
}

/*===========
 * Finalize Auto-Hunt system
 *------------------------------------------*/
void AutoHuntManager::finalize(void) {
	if (!initialized) {
		return;
	}

	// Stop all active auto-hunts
	for (auto& pair : data) {
		s_autohunt_data* ahd = &pair.second;
		if (ahd->active && ahd->timer_id != -1) {
			delete_timer(ahd->timer_id, autohunt_timer);
			ahd->timer_id = -1;
		}
	}

	data.clear();
	initialized = false;
}

/*===========
 * Get auto-hunt data for player
 *------------------------------------------*/
s_autohunt_data* AutoHuntManager::getData(int32 char_id) {
	auto it = data.find(char_id);
	if (it != data.end()) {
		return &it->second;
	}
	return nullptr;
}

/*===========
 * Remove auto-hunt data for player
 *------------------------------------------*/
void AutoHuntManager::removeData(int32 char_id) {
	auto it = data.find(char_id);
	if (it != data.end()) {
		s_autohunt_data* ahd = &it->second;
		if (ahd->active && ahd->timer_id != -1) {
			delete_timer(ahd->timer_id, autohunt_timer);
		}
		if (ahd->stuck_timer != -1) {
			delete_timer(ahd->stuck_timer, autohunt_timer);
		}
		data.erase(it);
	}
}

/*===========
 * Start auto-hunt for player
 *------------------------------------------*/
bool AutoHuntManager::start(int32 char_id) {
	map_session_data* sd = map_id2sd(char_id);
	if (!sd) {
		return false;
	}

	s_autohunt_data* ahd = getData(char_id);
	if (!ahd) {
		s_autohunt_data newdata;
		data[char_id] = newdata;
		ahd = &data[char_id];
	}

	if (ahd->active) {
		return true; // Already active
	}

	// Check if player can act
	if (pc_isdead(sd) || pc_issit(sd)) {
		return false;
	}

	// Start the timer
	ahd->active = true;
	ahd->state = AHUNT_SCANNING;
	ahd->target_id = 0;
	ahd->stuck_count = 0;
	ahd->teleport_count = 0;
	ahd->blacklist.clear();

	ahd->timer_id = add_timer_interval(
		gettick() + AUTOHUNT_INTERVAL,
		autohunt_timer,
		sd->id,
		0,
		AUTOHUNT_INTERVAL
	);

	clif_displaymessage(sd->fd, "[Auto-Hunt] Started.");
	ShowInfo("Auto-Hunt: Player %s started auto-hunt.\n", sd->status.name);
	return true;
}

/*===========
 * Stop auto-hunt for player
 *------------------------------------------*/
bool AutoHuntManager::stop(int32 char_id) {
	map_session_data* sd = map_id2sd(char_id);
	if (!sd) {
		return false;
	}

	s_autohunt_data* ahd = getData(char_id);
	if (!ahd || !ahd->active) {
		return true; // Already stopped
	}

	// Stop the timer
	if (ahd->timer_id != -1) {
		delete_timer(ahd->timer_id, autohunt_timer);
		ahd->timer_id = -1;
	}

	if (ahd->stuck_timer != -1) {
		delete_timer(ahd->stuck_timer, autohunt_timer);
		ahd->stuck_timer = -1;
	}

	// Stop any current action
	unit_stop_walking(sd, USW_FIXPOS);
	unit_stop_attack(sd);

	ahd->active = false;
	ahd->state = AHUNT_IDLE;
	ahd->target_id = 0;
	ahd->stuck_count = 0;

	clif_displaymessage(sd->fd, "[Auto-Hunt] Stopped.");
	ShowInfo("Auto-Hunt: Player %s stopped auto-hunt.\n", sd->status.name);
	return true;
}

/*===========
 * Toggle auto-hunt for player
 *------------------------------------------*/
bool AutoHuntManager::toggle(int32 char_id) {
	s_autohunt_data* ahd = getData(char_id);
	if (!ahd || !ahd->active) {
		return start(char_id);
	} else {
		return stop(char_id);
	}
}

/*===========
 * Check if auto-hunt is active
 *------------------------------------------*/
bool AutoHuntManager::isActive(int32 char_id) {
	s_autohunt_data* ahd = getData(char_id);
	return (ahd && ahd->active);
}

/*===========
 * Get config for player
 *------------------------------------------*/
s_autohunt_config* AutoHuntManager::getConfig(int32 char_id) {
	s_autohunt_data* ahd = getData(char_id);
	if (!ahd) {
		// Create entry
		s_autohunt_data newdata;
		data[char_id] = newdata;
		ahd = &data[char_id];
	}
	return &ahd->config;
}

/*===========
 * Set config for player
 *------------------------------------------*/
bool AutoHuntManager::setConfig(int32 char_id, const s_autohunt_config& config) {
	s_autohunt_data* ahd = getData(char_id);
	if (!ahd) {
		s_autohunt_data newdata;
		data[char_id] = newdata;
		ahd = &data[char_id];
	}
	ahd->config = config;
	return true;
}

/*===========
 * Main processing loop
 *------------------------------------------*/
void AutoHuntManager::process(int32 char_id, t_tick tick) {
	map_session_data* sd = map_id2sd(char_id);
	if (!sd) {
		stop(char_id);
		return;
	}

	s_autohunt_data* ahd = getData(char_id);
	if (!ahd || !ahd->active) {
		return;
	}

	// Check safety first
	if (!checkSafety(sd, ahd)) {
		return; // Player stopped auto-hunt due to safety condition
	}

	// Check if stuck (only when not walking and not attacking)
	if (ahd->state == AHUNT_MOVING) {
		if (sd->ud.walktimer == -1) {
			ahd->stuck_count++;
			if (ahd->stuck_count >= AUTOHUNT_STUCK_THRESHOLD) {
				// Blacklist this target before moving on
				if (ahd->target_id != 0) {
					ahd->blacklist[ahd->target_id] = gettick() + 10000; // skip for 10s
				}
				ShowInfo("Auto-Hunt: %s stuck, rescan (blacklisted target %d).\n", sd->status.name, ahd->target_id);
				ahd->target_id = 0;
				ahd->stuck_count = 0;

				// After 3 failed teleports, give up teleporting
				if (ahd->teleport_count >= 3) {
					ahd->state = AHUNT_SCANNING;
					ahd->teleport_count = 0;
				} else if (ahd->config.teleport_on_aggro) {
					ahd->state = AHUNT_TELEPORTING;
				} else {
					ahd->state = AHUNT_SCANNING;
				}
			}
		} else {
			ahd->stuck_count = 0;
		}
	}

	// Process current state
	switch (ahd->state) {
		case AHUNT_IDLE:
			processIdle(sd, ahd, tick);
			break;
		case AHUNT_SCANNING:
			processScanning(sd, ahd, tick);
			break;
		case AHUNT_MOVING:
			processMoving(sd, ahd, tick);
			break;
		case AHUNT_ATTACKING:
			processAttacking(sd, ahd, tick);
			break;
		case AHUNT_LOOTING:
			processLooting(sd, ahd, tick);
			break;
		case AHUNT_TELEPORTING:
			processTeleporting(sd, ahd, tick);
			break;
		case AHUNT_PAUSED:
			processPaused(sd, ahd, tick);
			break;
	}
}

/*===========
 * State: IDLE
 *------------------------------------------*/
void AutoHuntManager::processIdle(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	// Just scan for targets
	ahd->state = AHUNT_SCANNING;
}

/*===========
 * State: SCANNING
 *------------------------------------------*/
void AutoHuntManager::processScanning(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	if (findTarget(sd, ahd)) {
		block_list* bl = map_id2bl(ahd->target_id);
		if (bl) {
			ShowInfo("Auto-Hunt: %s found target at %d,%d (dist %d)\n",
				sd->status.name, bl->x, bl->y, distance_blxy(sd, bl->x, bl->y));
		}
		ahd->state = AHUNT_MOVING;
	} else {
		// No target found, stay in scanning state
	}
}

/*===========
 * State: MOVING
 *------------------------------------------*/
void AutoHuntManager::processMoving(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	block_list* bl = map_id2bl(ahd->target_id);
	if (!bl) {
		ahd->target_id = 0;
		ahd->state = AHUNT_SCANNING;
		return;
	}

	int16 dist = distance_blxy(sd, bl->x, bl->y);
	int16 range = 1;

	if (ahd->config.skill_id > 0) {
		range = skill_get_range(ahd->config.skill_id, ahd->config.skill_level);
	}

	if (dist <= range) {
		ahd->state = AHUNT_ATTACKING;
		return;
	}

	if (sd->ud.walktimer != -1) {
		return;
	}

	// Check if player can act
	if (pc_isdead(sd)) {
		return;
	}

	// Auto-stand if sitting
	if (pc_issit(sd)) {
		pc_setstand(sd, false);
	}

	// Force clear movement delay so autohunt can walk
	if (DIFF_TICK(sd->ud.canmove_tick, gettick()) > 0) {
		sd->ud.canmove_tick = gettick();
	}

	// Walk toward mob, but cap distance to avoid max_walk_path (default 17) failure
	int16 target_x = bl->x;
	int16 target_y = bl->y;
	int16 step_x = target_x - sd->x;
	int16 step_y = target_y - sd->y;
	int16 max_step = battle_config.max_walk_path - 2;

	int16 abs_x = abs(step_x);
	int16 abs_y = abs(step_y);
	if (abs_x > max_step || abs_y > max_step) {
		float len = (float)sqrt((double)(abs_x * abs_x + abs_y * abs_y));
		if (len > 0) {
			target_x = sd->x + (int16)((float)step_x / len * max_step);
			target_y = sd->y + (int16)((float)step_y / len * max_step);
		}
	}

	// Try walking to the (capped) target
	bool walk_result = unit_walktoxy(sd, target_x, target_y, 2);

	// If direct path blocked, try nearby cells to path around walls
	if (!walk_result) {
		int16 dir_x = (step_x > 0) ? 1 : (step_x < 0) ? -1 : 0;
		int16 dir_y = (step_y > 0) ? 1 : (step_y < 0) ? -1 : 0;
		// Try directions: toward target, then sideways, then cardinal
		int16 try_x[] = { dir_x, dir_x, 0, dir_x, -dir_x, 1, 0, -1, 0 };
		int16 try_y[] = { dir_y, 0, dir_y, -dir_y, dir_y, 0, 1, 0, -1 };
		for (int i = 0; i < 9; i++) {
			int16 nx = sd->x + try_x[i] * 3;
			int16 ny = sd->y + try_y[i] * 3;
			if (unit_walktoxy(sd, nx, ny, 2)) {
				walk_result = true;
				break;
			}
		}
	}

	if (!walk_result) {
		ShowInfo("Auto-Hunt: %s walk FAILED to %d,%d (player at %d,%d), rescan\n",
			sd->status.name, bl->x, bl->y, sd->x, sd->y);
		ahd->target_id = 0;
		ahd->state = AHUNT_SCANNING;
	}
}

/*===========
 * State: ATTACKING
 *------------------------------------------*/
void AutoHuntManager::processAttacking(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	block_list* bl = map_id2bl(ahd->target_id);
	if (!bl) {
		// Target died or despawned — check for loot
		if (findLootTarget(sd, ahd)) {
			ahd->state = AHUNT_LOOTING;
		} else {
			ahd->target_id = 0;
			ahd->state = AHUNT_SCANNING;
		}
		return;
	}

	// Check if target is dead
	if (status_isdead(*bl)) {
		ahd->target_id = 0;
		// Try to find loot nearby
		if (findLootTarget(sd, ahd)) {
			ahd->state = AHUNT_LOOTING;
		} else {
			ahd->state = AHUNT_SCANNING;
		}
		return;
	}

	// Check distance
	int16 dist = distance_blxy(sd, bl->x, bl->y);
	int16 range = 1;

	if (ahd->config.skill_id > 0) {
		range = skill_get_range(ahd->config.skill_id, ahd->config.skill_level);
	}

	// If too far, go back to moving
	if (dist > range) {
		ahd->state = AHUNT_MOVING;
		return;
	}

	// Try to use skill first
	if (ahd->config.skill_id > 0) {
		if (useSkill(sd, ahd)) {
			return; // Skill used successfully
		}
	}

	// Fall back to normal attack
	if (ahd->config.use_normal_attack) {
		unit_attack(sd, ahd->target_id, 0);
	}
}

/*===========
 * State: LOOTING
 *------------------------------------------*/
void AutoHuntManager::processLooting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	// If we have a loot target, try to pick it up
	if (ahd->target_id != 0) {
		block_list* bl = map_id2bl(ahd->target_id);
		if (bl && bl->type == BL_ITEM) {
			flooritem_data* fitem = (flooritem_data*)bl;
			int16 dist = distance_blxy(sd, bl->x, bl->y);

			// Close enough to pick up
			if (dist <= 1) {
				pc_takeitem(sd, fitem);
				ahd->target_id = 0;
				// Look for more loot nearby
				if (findLootTarget(sd, ahd)) {
					return; // Continue looting
				}
				ahd->state = AHUNT_SCANNING;
				return;
			}

			// Still walking to item
			if (sd->ud.walktimer != -1) {
				return;
			}

			// Force clear movement delay
			if (DIFF_TICK(sd->ud.canmove_tick, gettick()) > 0) {
				sd->ud.canmove_tick = gettick();
			}

			// Walk to item with capped distance (same as processMoving)
			int16 step_x = bl->x - sd->x;
			int16 step_y = bl->y - sd->y;
			int16 max_step = battle_config.max_walk_path - 2;
			int16 target_x = bl->x;
			int16 target_y = bl->y;

			int16 abs_x = abs(step_x);
			int16 abs_y = abs(step_y);
			if (abs_x > max_step || abs_y > max_step) {
				float len = (float)sqrt((double)(abs_x * abs_x + abs_y * abs_y));
				if (len > 0) {
					target_x = sd->x + (int16)((float)step_x / len * max_step);
					target_y = sd->y + (int16)((float)step_y / len * max_step);
				}
			}

			bool walk_result = unit_walktoxy(sd, target_x, target_y, 2);

			// If direct path blocked, try nearby cells (same fallback as processMoving)
			if (!walk_result) {
				int16 dir_x = (step_x > 0) ? 1 : (step_x < 0) ? -1 : 0;
				int16 dir_y = (step_y > 0) ? 1 : (step_y < 0) ? -1 : 0;
				int16 try_x[] = { dir_x, dir_x, 0, dir_x, -dir_x, 1, 0, -1, 0 };
				int16 try_y[] = { dir_y, 0, dir_y, -dir_y, dir_y, 0, 1, 0, -1 };
				for (int i = 0; i < 9; i++) {
					int16 nx = sd->x + try_x[i] * 3;
					int16 ny = sd->y + try_y[i] * 3;
					if (unit_walktoxy(sd, nx, ny, 2)) {
						walk_result = true;
						break;
					}
				}
			}

			if (!walk_result) {
				// Item unreachable - skip it
				ahd->target_id = 0;
				ahd->state = AHUNT_SCANNING;
			}
			return;
		}
		// Item gone
		ahd->target_id = 0;
	}

	// No loot target or item gone — look for more
	if (findLootTarget(sd, ahd)) {
		return;
	}

	ahd->state = AHUNT_SCANNING;
}

/*===========
 * State: TELEPORTING
 *------------------------------------------*/
void AutoHuntManager::processTeleporting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	if (doTeleport(sd, ahd)) {
		ahd->state = AHUNT_SCANNING;
	} else {
		// Failed to teleport, try again next tick
		// Or stop if out of fly wings
	}
}

/*===========
 * State: PAUSED
 *------------------------------------------*/
void AutoHuntManager::processPaused(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	// Try to use potion
	if (ahd->config.use_potion && ahd->config.potion_id != 0) {
		usePotion(sd, ahd);
	}

	// Check if HP/SP recovered
	int32 hp_percent = sd->battle_status.hp * 100 / sd->battle_status.max_hp;
	int32 sp_percent = sd->battle_status.sp * 100 / sd->battle_status.max_sp;

	if (hp_percent >= ahd->config.hp_threshold && sp_percent >= ahd->config.sp_threshold) {
		ahd->state = AHUNT_SCANNING;
	}
}

/*===========
 * Check safety conditions
 *------------------------------------------*/
bool AutoHuntManager::checkSafety(map_session_data* sd, s_autohunt_data* ahd) {
	// Check if dead
	if (pc_isdead(sd)) {
		stop(sd->status.char_id);
		return false;
	}

	// Check HP threshold (stop, don't pause)
	int32 hp_percent = sd->battle_status.hp * 100 / sd->battle_status.max_hp;
	if (ahd->config.hp_threshold > 0 && hp_percent < ahd->config.hp_threshold) {
		// Use potion first
		if (ahd->config.use_potion && ahd->config.potion_id != 0) {
			usePotion(sd, ahd);
		}

		// If still below threshold, pause
		hp_percent = sd->battle_status.hp * 100 / sd->battle_status.max_hp;
		if (hp_percent < ahd->config.hp_threshold) {
			if (ahd->state != AHUNT_PAUSED) {
				ahd->state = AHUNT_PAUSED;
				clif_displaymessage(sd->fd, "[Auto-Hunt] Paused: HP too low.");
			}
			return true; // Don't stop, just pause
		}
	}

	// Check SP threshold
	int32 sp_percent = sd->battle_status.sp * 100 / sd->battle_status.max_sp;
	if (ahd->config.sp_threshold > 0 && sp_percent < ahd->config.sp_threshold) {
		if (ahd->state != AHUNT_PAUSED) {
			ahd->state = AHUNT_PAUSED;
			clif_displaymessage(sd->fd, "[Auto-Hunt] Paused: SP too low.");
		}
		return true;
	}

	// Check weight
	if (pc_getpercentweight(*sd) >= 90) {
		if (ahd->state != AHUNT_PAUSED) {
			ahd->state = AHUNT_PAUSED;
			clif_displaymessage(sd->fd, "[Auto-Hunt] Paused: Weight too high.");
		}
		return true;
	}

	return true;
}

struct autohunt_target_data {
	map_session_data* sd;
	s_autohunt_data* ahd;
	int32 best_id;
	int16 best_dist;
	int16 best_level_diff;
};

/*===========
 * Callback: find nearest valid mob in range
 *------------------------------------------*/
static int32 autohunt_target_sub(block_list* bl, va_list ap) {
	autohunt_target_data* atd = va_arg(ap, autohunt_target_data*);

	if (!bl || bl->type != BL_MOB) {
		return 0;
	}

	map_session_data* sd = atd->sd;
	s_autohunt_data* ahd = atd->ahd;

	mob_data* md = BL_CAST(BL_MOB, bl);
	if (!md || !md->db) {
		return 0;
	}

	// Skip dead mobs
	if (status_isdead(*bl)) {
		return 0;
	}

	// Skip player's own summoned mobs (AI Companion, homunculus, etc.)
	if (md->master_id != 0) {
		return 0;
	}

	// Skip blacklisted targets (unreachable)
	if (ahd->blacklist.count(bl->id) && gettick() < ahd->blacklist[bl->id]) {
		return 0;
	}

	// Skip mobs that can't be attacked
	if (!status_has_mode(&md->status, MD_CANATTACK)) {
		return 0;
	}

	// Skip mobs targeting other players (optional: only attack mobs targeting us or idle)
	if (md->target_id != 0 && md->target_id != sd->id) {
		return 0;
	}

	// Filter by level range
	int16 mob_level = md->level;
	if (mob_level < ahd->config.target_min_level || mob_level > ahd->config.target_max_level) {
		return 0;
	}

	// Skip MVP/boss mobs unless explicitly configured
	if (status_has_mode(&md->status, MD_MVP)) {
		return 0;
	}

	int16 dist = distance_blxy(sd, bl->x, bl->y);

	// Pick the closest mob (ties broken by lowest level)
	int16 level_diff = static_cast<int16>(abs(static_cast<int32>(mob_level) - static_cast<int32>(sd->status.base_level)));
	if (atd->best_id == 0 || dist < atd->best_dist ||
		(dist == atd->best_dist && level_diff < atd->best_level_diff)) {
		atd->best_id = bl->id;
		atd->best_dist = dist;
		atd->best_level_diff = level_diff;
	}

	return 0;
}

/*===========
 * Find a target mob using area scan
 *------------------------------------------*/
bool AutoHuntManager::findTarget(map_session_data* sd, s_autohunt_data* ahd) {
	ahd->target_id = 0;

	int16 range = ahd->config.target_range;
	if (range > AUTOHUNT_MAX_RANGE) {
		range = AUTOHUNT_MAX_RANGE;
	}

	autohunt_target_data atd;
	atd.sd = sd;
	atd.ahd = ahd;
	atd.best_id = 0;
	atd.best_dist = 0;
	atd.best_level_diff = 0;

	map_foreachinrange(autohunt_target_sub, sd, range, BL_MOB, &atd);

	if (atd.best_id != 0) {
		ahd->target_id = atd.best_id;
		return true;
	}

	return false;
}

/*===========
 * Find loot target (items on ground)
 *------------------------------------------*/
/*===========
 * Find a loot target (floor items)
 *------------------------------------------*/
struct autohunt_loot_data {
	map_session_data* sd;
	int32 best_id;
	int16 best_dist;
};

int32 autohunt_loot_sub(block_list* bl, va_list ap) {
	autohunt_loot_data* ldt = va_arg(ap, autohunt_loot_data*);
	if (!bl || !ldt->sd) return 0;
	if (bl->type != BL_ITEM) return 0;

	int16 dist = distance_blxy(ldt->sd, bl->x, bl->y);
	if (ldt->best_id == 0 || dist < ldt->best_dist) {
		ldt->best_id = bl->id;
		ldt->best_dist = dist;
	}
	return 0;
}

bool AutoHuntManager::findLootTarget(map_session_data* sd, s_autohunt_data* ahd) {
	if (!ahd->config.loot_enabled) {
		return false;
	}

	autohunt_loot_data ldt;
	ldt.sd = sd;
	ldt.best_id = 0;
	ldt.best_dist = 0;

	int16 range = ahd->config.target_range;
	if (range > AUTOHUNT_MAX_RANGE) {
		range = AUTOHUNT_MAX_RANGE;
	}

	map_foreachinrange(autohunt_loot_sub, sd, range, BL_ITEM, &ldt);

	if (ldt.best_id != 0) {
		ahd->target_id = ldt.best_id;
		return true;
	}

	return false;
}

/*===========
 * Use skill on target
 *------------------------------------------*/
bool AutoHuntManager::useSkill(map_session_data* sd, s_autohunt_data* ahd) {
	if (ahd->config.skill_id == 0) {
		return false;
	}

	// Check if skill is available
	if (pc_checkskill(sd, ahd->config.skill_id) < ahd->config.skill_level) {
		return false;
	}

	// Check cooldown
	t_tick tick = gettick();
	auto it_scd = sd->scd.find(ahd->config.skill_id);
	if (it_scd != sd->scd.end() && it_scd->second > tick) {
		return false; // Still on cooldown
	}

	// Check SP
	int32 sp_cost = skill_get_sp(ahd->config.skill_id, ahd->config.skill_level);
	if (sd->battle_status.sp < sp_cost) {
		return false;
	}

	// Use the skill
	block_list* bl = map_id2bl(ahd->target_id);
	if (!bl) {
		return false;
	}

	// Determine skill type and use proper skill system (no blocking)
	if (skill_get_casttype(ahd->config.skill_id) == CAST_GROUND) {
		// Ground skill - use unit_skilluse_pos (handles cast time properly)
		unit_skilluse_pos(sd, bl->x, bl->y, ahd->config.skill_id, ahd->config.skill_level);
	} else {
		// Target skill - use unit_skilluse_id (handles cast time properly)
		unit_skilluse_id(sd, bl->id, ahd->config.skill_id, ahd->config.skill_level);
	}

	return true;
}

/*===========
 * Use potion
 *------------------------------------------*/
bool AutoHuntManager::usePotion(map_session_data* sd, s_autohunt_data* ahd) {
	if (ahd->config.potion_id == 0) {
		return false;
	}

	int32 idx = pc_search_inventory(sd, ahd->config.potion_id);
	if (idx < 0) {
		return false; // No potion in inventory
	}

	// Use the item
	pc_useitem(sd, idx);
	return true;
}

/*===========
 * Teleport using Fly Wing (from skill hotbar or inventory)
 *------------------------------------------*/
bool AutoHuntManager::doTeleport(map_session_data* sd, s_autohunt_data* ahd) {
	// 1. Check hotbar for teleport skill (AL_TELEPORT=26, NPC_RECALL=27, etc.)
	for (int32 i = 0; i < MAX_HOTKEYS_DB; i++) {
		if (sd->status.hotkeys[i].type == 1) { // skill type
			uint16 skill_id = sd->status.hotkeys[i].id;
			uint16 skill_lv = sd->status.hotkeys[i].lv;
			if (skill_id == 0) continue;
			// Known teleport skills: AL_TELEPORT(26), NPC_TELEPORT(many IDs)
			// Check if skill name contains "Teleport" or is known ID
			if (skill_id == AL_TELEPORT) {
				if (pc_checkskill(sd, skill_id) > 0) {
					int32 sp_cost = skill_get_sp(skill_id, skill_lv);
					if (sd->battle_status.sp >= sp_cost) {
						unit_skilluse_id(sd, sd->id, skill_id, skill_lv);
						ahd->target_id = 0;
						ahd->stuck_count = 0;
						ahd->teleport_count++;
						return true;
					}
				}
			}
		}
	}

	// 2. Fallback: search inventory for fly wing items (601, 12212, 12213, etc.)
	int32 flywing_ids[] = { 601, 12212, 12213, 12214, 12215, 12216, 12217 };
	for (int i = 0; i < sizeof(flywing_ids)/sizeof(flywing_ids[0]); i++) {
		int32 idx = pc_search_inventory(sd, flywing_ids[i]);
		if (idx >= 0) {
			pc_useitem(sd, idx);
			unit_stop_walking(sd, USW_FIXPOS);
			unit_stop_attack(sd);
			ahd->target_id = 0;
			ahd->stuck_count = 0;
			ahd->teleport_count++;
			return true;
		}
	}

	clif_displaymessage(sd->fd, "[Auto-Hunt] No teleport skill or Fly Wing found! Stopping.");
	stop(sd->status.char_id);
	return false;
}

/*===========
 * Send status message to player
 *------------------------------------------*/
void AutoHuntManager::autoDetectSkill(map_session_data* sd, s_autohunt_data* ahd) {
	if (!sd) return;

	for (int32 i = 0; i < MAX_HOTKEYS_DB; i++) {
		if (sd->status.hotkeys[i].type == 1) { // skill
			uint16 skill_id = sd->status.hotkeys[i].id;
			uint16 skill_lv = sd->status.hotkeys[i].lv;
			if (skill_id == 0) continue;
			if (pc_checkskill(sd, skill_id) <= 0) continue;

			// Check if it's an offensive skill (not buff/self)
			if (skill_get_inf(skill_id) & (INF_ATTACK_SKILL | INF_GROUND_SKILL)) {
				ahd->config.skill_id = skill_id;
				ahd->config.skill_level = skill_lv;
				char msg[256];
				snprintf(msg, sizeof(msg), "[Auto-Hunt] Auto-detected skill: %d (Lv %d)", skill_id, skill_lv);
				clif_displaymessage(sd->fd, msg);
				return;
			}
		}
	}

	// Fallback: no offensive skill found in hotbar
	ahd->config.skill_id = 0;
	ahd->config.skill_level = 1;
	clif_displaymessage(sd->fd, "[Auto-Hunt] No offensive skill found in hotbar. Using normal attack.");
}

void AutoHuntManager::sendStatus(map_session_data* sd, const char* msg) {
	if (sd) {
		clif_displaymessage(sd->fd, msg);
	}
}

/*===========
 * Timer callback
 *------------------------------------------*/
TIMER_FUNC(autohunt_timer) {
	// id = player GID
	map_session_data* sd = map_id2sd(id);
	if (!sd) {
		// Player not found, stop auto-hunt
		autohunt.removeData(id);
		return 0;
	}

	autohunt.process(id, tick);
	return 0;
}

/*===========
 * Initialize Auto-Hunt system
 *------------------------------------------*/
void do_init_autohunt(void) {
	add_timer_func_list(autohunt_timer, "autohunt_timer");
	ShowStatus("Auto-Hunt system initialized.\n");
}

/*===========
 * Finalize Auto-Hunt system
 *------------------------------------------*/
void do_final_autohunt(void) {
	// Stop all active auto-hunts
	for (auto& pair : autohunt.data) {
		s_autohunt_data* ahd = &pair.second;
		if (ahd->active && ahd->timer_id != -1) {
			delete_timer(ahd->timer_id, autohunt_timer);
		}
		if (ahd->stuck_timer != -1) {
			delete_timer(ahd->stuck_timer, autohunt_timer);
		}
	}
	autohunt.data.clear();
	ShowStatus("Auto-Hunt system finalized.\n");
}
