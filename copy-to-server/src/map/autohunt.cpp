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

AutoHuntManager autohunt;

#define AUTOHUNT_INTERVAL 500
#define AUTOHUNT_ITEMID_WING_OF_FLY 601
#define AUTOHUNT_MAX_RANGE 150
#define AUTOHUNT_STUCK_THRESHOLD 6

AutoHuntManager::AutoHuntManager() : initialized(false) {
}

AutoHuntManager::~AutoHuntManager() {
	finalize();
}

void AutoHuntManager::init(void) {
	if (initialized) return;
	add_timer_func_list(autohunt_timer, "autohunt_timer");
	initialized = true;
	ShowStatus("Auto-Hunt system initialized.\n");
}

void AutoHuntManager::finalize(void) {
	if (!initialized) return;
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

s_autohunt_data* AutoHuntManager::getData(int32 char_id) {
	auto it = data.find(char_id);
	if (it != data.end()) return &it->second;
	return nullptr;
}

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

void AutoHuntManager::logout(map_session_data* sd) {
	if (sd) removeData(sd->id);
}

bool AutoHuntManager::start(int32 char_id) {
	map_session_data* sd = map_id2sd(char_id);
	if (!sd) return false;

	s_autohunt_data* ahd = getData(char_id);
	if (!ahd) {
		s_autohunt_data newdata;
		data[char_id] = newdata;
		ahd = &data[char_id];
	}

	if (ahd->active) return true;

	if (pc_isdead(sd) || pc_issit(sd)) return false;

	if (ahd->config.teleport_on_aggro) {
		int32 idx = pc_search_inventory(sd, AUTOHUNT_ITEMID_WING_OF_FLY);
		if (idx < 0) {
			clif_displaymessage(sd->fd, "[Auto-Hunt] Need Fly Wing for teleport feature.");
			return false;
		}
	}

	ahd->active = true;
	ahd->state = AHUNT_SCANNING;
	ahd->target_id = 0;
	ahd->stuck_count = 0;

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

bool AutoHuntManager::stop(int32 char_id) {
	map_session_data* sd = map_id2sd(char_id);
	if (!sd) return false;

	s_autohunt_data* ahd = getData(char_id);
	if (!ahd || !ahd->active) return true;

	if (ahd->timer_id != -1) {
		delete_timer(ahd->timer_id, autohunt_timer);
		ahd->timer_id = -1;
	}
	if (ahd->stuck_timer != -1) {
		delete_timer(ahd->stuck_timer, autohunt_timer);
		ahd->stuck_timer = -1;
	}

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

bool AutoHuntManager::toggle(int32 char_id) {
	s_autohunt_data* ahd = getData(char_id);
	if (!ahd || !ahd->active) return start(char_id);
	else return stop(char_id);
}

bool AutoHuntManager::isActive(int32 char_id) {
	s_autohunt_data* ahd = getData(char_id);
	return (ahd && ahd->active);
}

s_autohunt_config* AutoHuntManager::getConfig(int32 char_id) {
	s_autohunt_data* ahd = getData(char_id);
	if (!ahd) {
		s_autohunt_data newdata;
		data[char_id] = newdata;
		ahd = &data[char_id];
	}
	return &ahd->config;
}

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

void AutoHuntManager::process(int32 char_id, t_tick tick) {
	map_session_data* sd = map_id2sd(char_id);
	if (!sd) { stop(char_id); return; }

	s_autohunt_data* ahd = getData(char_id);
	if (!ahd || !ahd->active) return;

	if (!checkSafety(sd, ahd)) return;

	if (ahd->state == AHUNT_MOVING) {
		if (sd->ud.walktimer == -1) {
			ahd->stuck_count++;
			if (ahd->stuck_count >= AUTOHUNT_STUCK_THRESHOLD) {
				ShowInfo("Auto-Hunt: Player %s stuck, rescanning.\n", sd->status.name);
				ahd->target_id = 0;
				ahd->state = AHUNT_SCANNING;
				ahd->stuck_count = 0;
			}
		} else {
			ahd->stuck_count = 0;
		}
	}

	switch (ahd->state) {
		case AHUNT_IDLE:       processIdle(sd, ahd, tick); break;
		case AHUNT_SCANNING:   processScanning(sd, ahd, tick); break;
		case AHUNT_MOVING:     processMoving(sd, ahd, tick); break;
		case AHUNT_ATTACKING:  processAttacking(sd, ahd, tick); break;
		case AHUNT_LOOTING:    processLooting(sd, ahd, tick); break;
		case AHUNT_TELEPORTING:processTeleporting(sd, ahd, tick); break;
		case AHUNT_PAUSED:     processPaused(sd, ahd, tick); break;
	}
}

void AutoHuntManager::processIdle(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	ahd->state = AHUNT_SCANNING;
}

void AutoHuntManager::processScanning(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	if (findTarget(sd, ahd)) {
		block_list* bl = map_id2bl(ahd->target_id);
		if (bl) {
			ShowInfo("Auto-Hunt: %s found target at %d,%d (dist %d)\n",
				sd->status.name, bl->x, bl->y, distance_blxy(sd, bl->x, bl->y));
		}
		ahd->state = AHUNT_MOVING;
	}
}

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

	if (sd->ud.walktimer != -1) return;
	if (pc_isdead(sd)) return;

	if (pc_issit(sd)) {
		pc_setstand(sd, false);
	}

	if (DIFF_TICK(sd->ud.canmove_tick, gettick()) > 0) {
		sd->ud.canmove_tick = gettick();
	}

	bool walk_result = unit_walktobl(sd, bl, 1, 0);
	if (!walk_result) {
		walk_result = unit_walktoxy(sd, bl->x, bl->y, 0);
	}
	if (!walk_result) {
		int16 dx = 0, dy = 0;
		if (bl->x > sd->x) dx = 1;
		else if (bl->x < sd->x) dx = -1;
		if (bl->y > sd->y) dy = 1;
		else if (bl->y < sd->y) dy = -1;
		if (dx != 0 || dy != 0) {
			walk_result = unit_walktoxy(sd, sd->x + dx * 5, sd->y + dy * 5, 0);
		}
	}
	if (!walk_result) {
		ShowInfo("Auto-Hunt: %s walk ALL FAILED to %d,%d (player at %d,%d), rescan\n",
			sd->status.name, bl->x, bl->y, sd->x, sd->y);
		ahd->target_id = 0;
		ahd->state = AHUNT_SCANNING;
	}
}

void AutoHuntManager::processAttacking(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
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

	if (dist > range) {
		ahd->state = AHUNT_MOVING;
		return;
	}

	if (ahd->config.skill_id > 0) {
		if (useSkill(sd, ahd)) return;
	}

	if (ahd->config.use_normal_attack) {
		unit_attack(sd, ahd->target_id, 0);
	}
}

void AutoHuntManager::processLooting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	if (ahd->target_id != 0) {
		block_list* bl = map_id2bl(ahd->target_id);
		if (bl && bl->type == BL_ITEM) {
			flooritem_data* fitem = (flooritem_data*)bl;
			int16 dist = distance_blxy(sd, bl->x, bl->y);
			if (dist <= 1) {
				pc_takeitem(sd, fitem);
				ahd->target_id = 0;
				ahd->state = AHUNT_SCANNING;
				return;
			}
			if (sd->ud.walktimer == -1) {
				if (!unit_walktoxy(sd, bl->x, bl->y, 2)) {
					ahd->target_id = 0;
					ahd->state = AHUNT_SCANNING;
				}
			}
			return;
		}
		ahd->target_id = 0;
	}
	ahd->state = AHUNT_SCANNING;
}

void AutoHuntManager::processTeleporting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	if (doTeleport(sd, ahd)) {
		ahd->state = AHUNT_SCANNING;
	}
}

void AutoHuntManager::processPaused(map_session_data* sd, s_autohunt_data* ahd, t_tick tick) {
	if (ahd->config.use_potion && ahd->config.potion_id != 0) {
		usePotion(sd, ahd);
	}

	int32 hp_percent = sd->battle_status.hp * 100 / sd->battle_status.max_hp;
	int32 sp_percent = sd->battle_status.sp * 100 / sd->battle_status.max_sp;

	if (hp_percent >= ahd->config.hp_threshold && sp_percent >= ahd->config.sp_threshold) {
		ahd->state = AHUNT_SCANNING;
	}
}

bool AutoHuntManager::checkSafety(map_session_data* sd, s_autohunt_data* ahd) {
	if (pc_isdead(sd)) {
		stop(sd->status.char_id);
		return false;
	}

	int32 hp_percent = sd->battle_status.hp * 100 / sd->battle_status.max_hp;
	if (ahd->config.hp_threshold > 0 && hp_percent < ahd->config.hp_threshold) {
		if (ahd->config.use_potion && ahd->config.potion_id != 0) {
			usePotion(sd, ahd);
		}
		hp_percent = sd->battle_status.hp * 100 / sd->battle_status.max_hp;
		if (hp_percent < ahd->config.hp_threshold) {
			if (ahd->state != AHUNT_PAUSED) {
				ahd->state = AHUNT_PAUSED;
				clif_displaymessage(sd->fd, "[Auto-Hunt] Paused: HP too low.");
			}
			return true;
		}
	}

	int32 sp_percent = sd->battle_status.sp * 100 / sd->battle_status.max_sp;
	if (ahd->config.sp_threshold > 0 && sp_percent < ahd->config.sp_threshold) {
		if (ahd->state != AHUNT_PAUSED) {
			ahd->state = AHUNT_PAUSED;
			clif_displaymessage(sd->fd, "[Auto-Hunt] Paused: SP too low.");
		}
		return true;
	}

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

static int32 autohunt_target_sub(block_list* bl, va_list ap) {
	autohunt_target_data* atd = va_arg(ap, autohunt_target_data*);
	if (!bl || bl->type != BL_MOB) return 0;

	map_session_data* sd = atd->sd;
	s_autohunt_data* ahd = atd->ahd;

	mob_data* md = BL_CAST(BL_MOB, bl);
	if (!md || !md->db) return 0;
	if (status_isdead(*bl)) return 0;
	if (md->master_id != 0) return 0;
	if (!status_has_mode(&md->status, MD_CANATTACK)) return 0;
	if (md->target_id != 0 && md->target_id != sd->id) return 0;

	int16 mob_level = md->level;
	if (mob_level < ahd->config.target_min_level || mob_level > ahd->config.target_max_level) return 0;
	if (status_has_mode(&md->status, MD_MVP)) return 0;

	int16 dist = distance_blxy(sd, bl->x, bl->y);
	int16 level_diff = static_cast<int16>(abs(static_cast<int32>(mob_level) - static_cast<int32>(sd->status.base_level)));
	if (atd->best_id == 0 || dist < atd->best_dist ||
		(dist == atd->best_dist && level_diff < atd->best_level_diff)) {
		atd->best_id = bl->id;
		atd->best_dist = dist;
		atd->best_level_diff = level_diff;
	}

	return 0;
}

bool AutoHuntManager::findTarget(map_session_data* sd, s_autohunt_data* ahd) {
	ahd->target_id = 0;

	int16 range = ahd->config.target_range;
	if (range > AUTOHUNT_MAX_RANGE) range = AUTOHUNT_MAX_RANGE;

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
	if (!ahd->config.loot_enabled) return false;

	autohunt_loot_data ldt;
	ldt.sd = sd;
	ldt.best_id = 0;
	ldt.best_dist = 0;

	map_foreachinrange(autohunt_loot_sub, sd, 15, BL_ITEM, &ldt);

	if (ldt.best_id != 0) {
		ahd->target_id = ldt.best_id;
		return true;
	}
	return false;
}

bool AutoHuntManager::useSkill(map_session_data* sd, s_autohunt_data* ahd) {
	if (ahd->config.skill_id == 0) return false;
	if (pc_checkskill(sd, ahd->config.skill_id) < ahd->config.skill_level) return false;

	t_tick tick = gettick();
	auto it_scd = sd->scd.find(ahd->config.skill_id);
	if (it_scd != sd->scd.end() && it_scd->second > tick) return false;

	int32 sp_cost = skill_get_sp(ahd->config.skill_id, ahd->config.skill_level);
	if (sd->battle_status.sp < sp_cost) return false;

	block_list* bl = map_id2bl(ahd->target_id);
	if (!bl) return false;

	if (skill_get_casttype(ahd->config.skill_id) == CAST_GROUND) {
		skill_castend_pos2(sd, bl->x, bl->y, ahd->config.skill_id, ahd->config.skill_level, tick, 0);
	} else {
		skill_castend_damage_id(sd, bl, ahd->config.skill_id, ahd->config.skill_level, tick, 0);
	}

	return true;
}

bool AutoHuntManager::usePotion(map_session_data* sd, s_autohunt_data* ahd) {
	if (ahd->config.potion_id == 0) return false;
	int32 idx = pc_search_inventory(sd, ahd->config.potion_id);
	if (idx < 0) return false;
	pc_useitem(sd, idx);
	return true;
}

bool AutoHuntManager::doTeleport(map_session_data* sd, s_autohunt_data* ahd) {
	int32 idx = pc_search_inventory(sd, AUTOHUNT_ITEMID_WING_OF_FLY);
	if (idx < 0) {
		clif_displaymessage(sd->fd, "[Auto-Hunt] No Fly Wings! Stopping.");
		stop(sd->status.char_id);
		return false;
	}
	pc_useitem(sd, idx);
	unit_stop_walking(sd, USW_FIXPOS);
	unit_stop_attack(sd);
	ahd->target_id = 0;
	ahd->stuck_count = 0;
	return true;
}

void AutoHuntManager::autoDetectSkill(map_session_data* sd, s_autohunt_data* ahd) {
	if (!sd) return;

	for (int32 i = 0; i < MAX_HOTKEYS_DB; i++) {
		if (sd->status.hotkeys[i].type == 1) {
			uint16 skill_id = sd->status.hotkeys[i].id;
			uint16 skill_lv = sd->status.hotkeys[i].lv;
			if (skill_id == 0) continue;
			if (pc_checkskill(sd, skill_id) <= 0) continue;
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

	ahd->config.skill_id = 0;
	ahd->config.skill_level = 1;
	clif_displaymessage(sd->fd, "[Auto-Hunt] No offensive skill found in hotbar. Using normal attack.");
}

void AutoHuntManager::sendStatus(map_session_data* sd, const char* msg) {
	if (sd) clif_displaymessage(sd->fd, msg);
}

TIMER_FUNC(autohunt_timer) {
	map_session_data* sd = map_id2sd(id);
	if (!sd) {
		autohunt.removeData(id);
		return 0;
	}
	autohunt.process(id, tick);
	return 0;
}

void do_init_autohunt(void) {
	add_timer_func_list(autohunt_timer, "autohunt_timer");
	ShowStatus("Auto-Hunt system initialized.\n");
}

void do_final_autohunt(void) {
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
