//=============================================================
// Auto-Hunt System for rAthena
// Version: 3.0
// Changelog:
//   v1.0 - Initial implementation (state machine, target scan, attack, loot, teleport)
//   v1.1 - Config system (@autohunt config skill/hp/sp/range/potion)
//   v1.2 - Auto-detect skill from hotbar (autoDetectSkill)
//   v1.3 - NPC config menu (autohunt_npc.txt)
//   v1.4 - Auto-potion system
//   v1.5 - Auto-looting rewrite (map_foreachinrange + BL_ITEM)
//   v2.0 - Movement fixes:
//          - Fixed canmove_tick blocking (force reset)
//          - Fixed max_walk_path=17 overflow (capped steps)
//          - Walk fallback: try 9 directions around walls
//          - Skip companion mobs (master_id != 0)
//          - Auto-stand if sitting
//   v2.1 - Auto-looting fix:
//          - Check mob death in processAttacking → transition to LOOTING
//          - findLootTarget actually called now
//          - processLooting uses capped steps (same as processMoving)
//          - Chain loot: pick up multiple items nearby
//   v3.0 - Major fixes:
//          - Teleport: stuck_count triggers TELEPORTING after threshold
//          - Target blacklist: skip unreachable targets for 10 seconds
//          - Skill cast: use unit_skilluse_id/pos (no blocking)
//          - Loot range: configurable (default 30, same as target_range)
//          - Loot capped walk: same fallback as processMoving
//=============================================================

#ifndef AUTOHUNT_HPP
#define AUTOHUNT_HPP

#include "../common/cbasetypes.hpp"
#include "../common/mmo.hpp"
#include <unordered_map>
#include <vector>

class map_session_data;

// Auto-Hunt States
enum e_autohunt_state : uint8 {
	AHUNT_IDLE = 0,       // Not hunting
	AHUNT_SCANNING,       // Looking for target
	AHUNT_MOVING,         // Walking to target
	AHUNT_ATTACKING,      // Using skill or normal attack
	AHUNT_LOOTING,        // Picking up items
	AHUNT_TELEPORTING,    // Escaping danger
	AHUNT_PAUSED,         // HP/SP too low, waiting
};

// Auto-Hunt Configuration (per player)
struct s_autohunt_config {
	// Target settings
	int16  target_min_level;       // Minimum monster level
	int16  target_max_level;       // Maximum monster level
	int16  target_range;           // Search range in cells (default: 14)

	// Skill settings
	uint16 skill_id;               // Skill to use (0 = normal attack only)
	uint16 skill_level;            // Skill level
	bool   use_normal_attack;      // Use normal attack when skill on cooldown

	// Safety settings
	int16  hp_threshold;           // Stop when HP% below this (0 = disabled)
	int16  sp_threshold;           // Stop when SP% below this (0 = disabled)
	bool   use_potion;            // Auto-use HP potions
	t_itemid potion_id;           // Item ID of potion to use

	// Teleport settings
	bool   teleport_on_aggro;     // Teleport when attacked by unwanted mob

	// Loot settings
	bool   loot_enabled;          // Pick up items automatically
	bool   loot_weapons;          // Pick up weapons
	bool   loot_armors;           // Pick up armors

	s_autohunt_config() :
		target_min_level(1),
		target_max_level(999),
		target_range(30),
		skill_id(0),
		skill_level(1),
		use_normal_attack(true),
		hp_threshold(30),
		sp_threshold(10),
		use_potion(true),
		potion_id(0),
		teleport_on_aggro(false),
		loot_enabled(true),
		loot_weapons(true),
		loot_armors(true)
	{}
};

// Auto-Hunt Runtime Data (per player)
struct s_autohunt_data {
	bool               active;       // Is auto-hunt running?
	e_autohunt_state   state;        // Current state
	int32              target_id;    // Current target entity ID (mob or item)
	int32              timer_id;     // Main loop timer ID
	int32              stuck_timer;  // Timer to detect if stuck
	int16              stuck_x;      // Last known position
	int16              stuck_y;      // Last known position
	uint8              stuck_count;  // How many ticks stuck
	uint8              teleport_count; // Consecutive teleports (give up after 3)

	// Target blacklist: target_id -> expiry tick (skip unreachable targets)
	std::unordered_map<int32, t_tick> blacklist;

	s_autohunt_config  config;       // Player configuration

	s_autohunt_data() :
		active(false),
		state(AHUNT_IDLE),
		target_id(0),
		timer_id(-1),
		stuck_timer(-1),
		stuck_x(0),
		stuck_y(0),
		stuck_count(0),
		teleport_count(0)
	{}
};

// Auto-Hunt Manager
class AutoHuntManager {
public:
	std::unordered_map<int32, s_autohunt_data> data; // key = account_id (sd->id)

private:
	bool initialized;

public:
	AutoHuntManager();
	~AutoHuntManager();

	// Initialize/Cleanup
	void init(void);
	void finalize(void);

	// Per-player data management
	s_autohunt_data* getData(int32 char_id);
	void removeData(int32 char_id);

	// Start/Stop auto-hunt
	bool start(int32 char_id);
	bool stop(int32 char_id);
	bool toggle(int32 char_id);
	bool isActive(int32 char_id);

	// Configuration
	s_autohunt_config* getConfig(int32 char_id);
	bool setConfig(int32 char_id, const s_autohunt_config& config);
	void autoDetectSkill(map_session_data* sd, s_autohunt_data* ahd);

	// Main loop (called by timer)
	void process(int32 char_id, t_tick tick);

private:
	// State handlers
	void processIdle(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processScanning(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processMoving(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processAttacking(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processLooting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processTeleporting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processPaused(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);

	// Helper functions
	bool checkSafety(map_session_data* sd, s_autohunt_data* ahd);
	bool findTarget(map_session_data* sd, s_autohunt_data* ahd);
	bool findLootTarget(map_session_data* sd, s_autohunt_data* ahd);
	bool useSkill(map_session_data* sd, s_autohunt_data* ahd);
	bool usePotion(map_session_data* sd, s_autohunt_data* ahd);
	bool doTeleport(map_session_data* sd, s_autohunt_data* ahd);
	void sendStatus(map_session_data* sd, const char* msg);
};

// Global instance
extern AutoHuntManager autohunt;

// Timer function declaration
TIMER_FUNC(autohunt_timer);

// Init/Final
void do_init_autohunt(void);
void do_final_autohunt(void);

#endif // AUTOHUNT_HPP
