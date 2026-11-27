

#include "Features.h"
#include "Menu.h"
#include "Engine.h"
#include "Vehicles.h"
#include "Extras.h"
#include <stdio.h>

// Everything here only queues work; the game thread runs it (Engine::GameTick from the pad hook).

namespace Features {

namespace {
	bool g_neverWanted = false;
	bool g_freezeTime = false;
	int g_hour = 12;
	int g_wanted = 0;
	int g_model = 0;
	bool g_god = false, g_superJump = false, g_vehGod = false, g_infClip = false;
	int g_item = 0;
	int g_locCat = 0, g_locIdx = 0, g_door = 0;
	bool g_meteors = false, g_blackHole = false;
	int g_weather = 0, g_explosion = 0, g_location = 0;

	bool Off() { return !Engine::Ready(); }

	int IndexOf(const char* name)
	{
		for (int i = 0; i < kVehicleModelCount; i++) {
			const char* a = kVehicleModels[i].name; const char* b = name;
			while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
			if (!*a && !*b) return i;
		}
		return 0;
	}

	void SelfMenu()
	{
		if (Menu::Toggle("God mode", &g_god, Off())) Engine::SetGodMode(g_god);
		if (Menu::Toggle("Super jump (Square)", &g_superJump, Off())) Engine::SetSuperJump(g_superJump);
		if (Menu::Toggle("No-clip (hold L3 to fly)", &g_infClip, Off())) Engine::SetInfiniteClip(g_infClip);
		if (Menu::Action("Heal + armour + repair car", Off())) Engine::Queue(Engine::Op::HealRepair);
		if (Menu::Int("Set wanted level", &g_wanted, 0, 5, 1, Off())) Engine::Queue(Engine::Op::SetWanted, g_wanted);
		if (Menu::Toggle("Never wanted", &g_neverWanted, Off())) Engine::SetNeverWanted(g_neverWanted);
		if (Menu::Action("Wanted level +1 (cheat)", Off())) Engine::Queue(Engine::Op::WantedUp);
		if (Menu::Action("Wanted level -1 (cheat)", Off())) Engine::Queue(Engine::Op::WantedDown);
	}

	void VehicleMenu()
	{
		// Left/Right picks a model by name, Cross on the row (or "Spawn") spawns it.
		char label[64];
		const char* display = Extras::VehicleLabel(g_model);
		if (display[0]) snprintf(label, sizeof(label), "%s (%s)", display, kVehicleModels[g_model].name);
		else snprintf(label, sizeof(label), "Model: %s", kVehicleModels[g_model].name);
		Menu::Int(label, &g_model, 0, kVehicleModelCount - 1, 1, Off());
		if (!Off()) Menu::Warn(!Engine::ModelAvailable(kVehicleModels[g_model].hash));   // red = not registered, spawn won't work
		if (Menu::Action("Spawn selected vehicle", Off())) Engine::Queue(Engine::Op::SpawnModel, g_model);
		if (Menu::Action("Quick: sanchez", Off())) Engine::Queue(Engine::Op::SpawnModel, IndexOf("sanchez"));
		if (Menu::Toggle("Vehicle god mode", &g_vehGod, Off())) Engine::SetVehicleGod(g_vehGod);
		char doorLabel[40];
		snprintf(doorLabel, sizeof(doorLabel), "Door / part %d", g_door);
		Menu::Int(doorLabel, &g_door, 0, 7, 1, Off());
		if (Menu::Action("Open part", Off())) Extras::RequestDoor(g_door, true);
		if (Menu::Action("Close part", Off())) Extras::RequestDoor(g_door, false);
		if (Menu::Action("Engine on", Off())) Extras::RequestEngine(true);
		if (Menu::Action("Engine off", Off())) Extras::RequestEngine(false);
		if (Menu::Action("Speed boost", Off())) Engine::Queue(Engine::Op::VehicleBoost);
		if (Menu::Action("Vehicle jump", Off())) Engine::Queue(Engine::Op::VehicleJump);
	}

	void MoneyMenu()
	{
		char sl[64];
		snprintf(sl, sizeof(sl), "Stash: $%lld", (long long)Extras::StashCash());
		Menu::Label(sl);
		if (Menu::Action("Fill stash ($20,000)", Off())) Extras::RequestSetCash(true, 20000, false);
		if (Menu::Action("Fill cash on hand ($19,999)", Off())) Extras::RequestSetCash(false, 19999, false);
		Menu::Label(Extras::LastResult());
	}


	void LocationsMenu()
	{
		int nc = Extras::LocationCategoryCount();
		if (!nc) { Menu::Label("Locations not loaded"); return; }
		if (g_locCat >= nc) g_locCat = 0;
		int n = Extras::LocationCountIn(g_locCat);
		if (g_locIdx >= n) g_locIdx = 0;
		char label[64];
		snprintf(label, sizeof(label), "%s (%d)", Extras::LocationCategory(g_locCat), n);
		if (Menu::Int(label, &g_locCat, 0, nc - 1, 1, Off())) g_locIdx = 0;
		if (n) {
			snprintf(label, sizeof(label), "%d/%d %s", g_locIdx + 1, n, Extras::LocationName(g_locCat, g_locIdx));
			Menu::Int(label, &g_locIdx, 0, n - 1, 1, Off());
			if (Menu::Action("Teleport there", Off())) {
				float x, y, z;
				Extras::LocationPos(g_locCat, g_locIdx, &x, &y, &z);
				Engine::TeleportTo(x, y, z);
			}
		}
	}


	void FunMenu()
	{
		char label[48];
		snprintf(label, sizeof(label), "Explosion type %d", g_explosion);
		Menu::Int(label, &g_explosion, 0, 68, 1, Off());
		if (Menu::Action("Explode 10 m ahead", Off())) Extras::RequestExplosion(g_explosion);
		if (Menu::Action("Spawn clone of me", Off())) Extras::RequestClone();
		if (Menu::Toggle("Meteor shower", &g_meteors, Off())) Extras::SetMeteorShower(g_meteors);
		if (Menu::Toggle("Black hole", &g_blackHole, Off())) Extras::SetBlackHole(g_blackHole);
		if (Menu::Action("Explode all vehicles", Off())) Extras::RequestExplodeAll(true);
		if (Menu::Action("Kill all peds (explosions)", Off())) Extras::RequestExplodeAll(false);
		if (Menu::Action("Delete nearby vehicles", Off())) Extras::RequestDeleteNearby(true, false);
		if (Menu::Action("Delete nearby peds", Off())) Extras::RequestDeleteNearby(false, true);
		if (Menu::Action("Clear area (both)", Off())) Extras::RequestDeleteNearby(true, true);
		Menu::Label(Extras::LastResult());
	}

	void WeatherMenu()
	{
		int n = Extras::WeatherCount();
		if (!n) { Menu::Label("Weather list not loaded yet"); return; }
		if (g_weather >= n) g_weather = 0;
		uint32_t h = Extras::WeatherHash(g_weather);
		const char* name = Extras::WeatherName(h);
		char label[64];
		if (name) snprintf(label, sizeof(label), "Weather: %s", name);
		else snprintf(label, sizeof(label), "Weather %d: %08X", g_weather, h);
		Menu::Int(label, &g_weather, 0, n - 1, 1, Off());
		if (Menu::Action("Set weather now", Off())) Extras::RequestWeather(g_weather);
	}

	void TeleportMenu()
	{
		if (Menu::Action("Teleport forward 5 m", Off())) Engine::Queue(Engine::Op::TeleportForward);
		if (Menu::Action("Teleport up 50 m", Off())) Engine::Queue(Engine::Op::TeleportUp);
		if (Menu::Action("Save this location", Off())) Engine::Queue(Engine::Op::SaveLocation);
		int n = Engine::LocationCount();
		if (n) {
			if (g_location >= n) g_location = n - 1;
			char label[64], text[48];
			Engine::LocationText(g_location, text, sizeof(text));
			snprintf(label, sizeof(label), "Saved %s", text);
			Menu::Int(label, &g_location, 0, n - 1, 1, Off());
			if (Menu::Action("Go to saved location", Off())) Engine::Queue(Engine::Op::GotoLocation, g_location);
		}
	}


	void ItemsMenu()
	{
		int n = Engine::ItemCount();
		if (!n) { Menu::Label("Item list not loaded yet"); return; }
		if (g_item >= n) g_item = 0;
		uint32_t h = Engine::ItemHash(g_item);
		const char* name = Engine::ItemName(h);
		const char* game = Extras::ItemLabel(h);
		char label[64];
		if (game[0]) snprintf(label, sizeof(label), "%s", game);
		else if (name) snprintf(label, sizeof(label), "Item: %s", name);
		else snprintf(label, sizeof(label), "Item %d: %08X", g_item, h);
		Menu::Int(label, &g_item, 0, n - 1, 1, Off());
		if (Menu::Action("Give item", Off())) Engine::Queue(Engine::Op::GiveItem, g_item);
		if (Menu::Action("Give item x10", Off()))
			for (int i = 0; i < 10; i++) Engine::Queue(Engine::Op::GiveItem, g_item);
	}

	void WorldMenu()
	{
		if (Menu::Int("Set hour", &g_hour, 0, 23, 1, Off())) Engine::Queue(Engine::Op::SetHour, g_hour);
		if (Menu::Toggle("Freeze time", &g_freezeTime, Off())) Engine::SetFreezeTime(g_freezeTime);
		if (Menu::Action("Next weather", Off())) Engine::Queue(Engine::Op::NextWeather);
	}


	void RootMenu()
	{
		const char* why = Engine::LockReason();
		if (why) {
			char line[64];
			snprintf(line, sizeof(line), "Locked: %s", why);
			Menu::Label(line);
		}
		Menu::Sub("Self", SelfMenu);
		Menu::Sub("Vehicle", VehicleMenu);
		Menu::Sub("Teleport", TeleportMenu);
		Menu::Sub("Locations", LocationsMenu);
		Menu::Sub("World", WorldMenu);
		Menu::Sub("Weather", WeatherMenu);
		Menu::Sub("Fun", FunMenu);
		Menu::Sub("Money", MoneyMenu);
		Menu::Sub("Items", ItemsMenu);
	}
}

void Init()
{
	Menu::SetRoot("Basic Menu", RootMenu);
}

void Tick()
{
	// Game-side work runs on the game thread (Engine::GameTick); nothing to do here.
}

}
