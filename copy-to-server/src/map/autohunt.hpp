#ifndef AUTOHUNT_HPP
#define AUTOHUNT_HPP

#include "../common/cbasetypes.hpp"
#include "../common/mmo.hpp"
#include <unordered_map>

class map_session_data;

// Auto-Hunt States
enum e_autohunt_state : uint8 {
	AHUNT_IDLE = 0,
	AHUNT_SCANNING,
	AHUNT_MOVING,
	AHUNT_ATTACKING,
	AHUNT_LOOTING,
	AHUNT_TELEPORTING,
	AHUNT_PAUSED,
};

// Auto-Hunt Configuration (per player)
struct s_autohunt_config {
	int16  target_min_level;
	int16  target_max_level;
	int16  target_range;

	uint16 skill_id;
	uint16 skill_level;
	bool   use_normal_attack;

	int16  hp_threshold;
	int16  sp_threshold;
	bool   use_potion;
	t_itemid potion_id;

	bool   teleport_on_aggro;

	bool   loot_enabled;
	bool   loot_weapons;
	bool   loot_armors;

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
	bool               active;
	e_autohunt_state   state;
	int32              target_id;
	int32              timer_id;
	int32              stuck_timer;
	int16              stuck_x;
	int16              stuck_y;
	uint8              stuck_count;

	s_autohunt_config  config;

	s_autohunt_data() :
		active(false),
		state(AHUNT_IDLE),
		target_id(0),
		timer_id(-1),
		stuck_timer(-1),
		stuck_x(0),
		stuck_y(0),
		stuck_count(0)
	{}
};

// Auto-Hunt Manager
class AutoHuntManager {
public:
	std::unordered_map<int32, s_autohunt_data> data;

private:
	bool initialized;

public:
	AutoHuntManager();
	~AutoHuntManager();

	void init(void);
	void finalize(void);

	s_autohunt_data* getData(int32 char_id);
	void removeData(int32 char_id);

	bool start(int32 char_id);
	bool stop(int32 char_id);
	bool toggle(int32 char_id);
	bool isActive(int32 char_id);

	s_autohunt_config* getConfig(int32 char_id);
	bool setConfig(int32 char_id, const s_autohunt_config& config);
	void autoDetectSkill(map_session_data* sd, s_autohunt_data* ahd);

	void process(int32 char_id, t_tick tick);
	void logout(map_session_data* sd);

private:
	void processIdle(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processScanning(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processMoving(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processAttacking(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processLooting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processTeleporting(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);
	void processPaused(map_session_data* sd, s_autohunt_data* ahd, t_tick tick);

	bool checkSafety(map_session_data* sd, s_autohunt_data* ahd);
	bool findTarget(map_session_data* sd, s_autohunt_data* ahd);
	bool findLootTarget(map_session_data* sd, s_autohunt_data* ahd);
	bool useSkill(map_session_data* sd, s_autohunt_data* ahd);
	bool usePotion(map_session_data* sd, s_autohunt_data* ahd);
	bool doTeleport(map_session_data* sd, s_autohunt_data* ahd);
	void sendStatus(map_session_data* sd, const char* msg);
};

extern AutoHuntManager autohunt;

TIMER_FUNC(autohunt_timer);

void do_init_autohunt(void);
void do_final_autohunt(void);

#endif // AUTOHUNT_HPP
