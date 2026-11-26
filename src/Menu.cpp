#include "Menu.h"
#include "Input.h"
#include "Overlay.h"
#include <pad.h>
#include <stdio.h>

namespace Menu {

namespace {
	const uint32_t kOpen = SCE_PAD_BUTTON_R3 | SCE_PAD_BUTTON_RIGHT;
	const uint32_t kUsed = SCE_PAD_BUTTON_UP | SCE_PAD_BUTTON_DOWN | SCE_PAD_BUTTON_LEFT | SCE_PAD_BUTTON_RIGHT |
		SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE | SCE_PAD_BUTTON_R3;
	const int kMaxDepth = 8, kMaxItems = 64;

	struct Level { Page page; char title[48]; int selected; };
	Level s_stack[kMaxDepth];
	int s_depth = 0;
	bool s_open = false;

	Item s_items[kMaxItems];
	int s_count = 0;

	bool s_select = false, s_left = false, s_right = false;
	Page s_next = nullptr;
	const char* s_nextTitle = nullptr;

	Item* Add(const char* text, bool disabled)
	{
		if (s_count >= kMaxItems) return nullptr;
		Item* it = &s_items[s_count++];
		snprintf(it->text, sizeof(it->text), "%s", text);
		it->value[0] = 0;
		it->disabled = disabled;
		it->keepCase = false;
		return it;
	}

	bool Current() { return s_count - 1 == s_stack[s_depth].selected; }
}

void SetRoot(const char* title, Page root)
{
	s_stack[0].page = root;
	snprintf(s_stack[0].title, sizeof(s_stack[0].title), "%s", title);
	s_stack[0].selected = 0;
	s_depth = 0;
}

bool IsOpen() { return s_open; }

void Tick()
{
	Input::Poll();
	uint32_t held = Input::Held();

	if (!s_open) {
		if ((held & kOpen) == kOpen && (Input::Pressed() & kOpen)) {
			s_open = true;
			Input::Block(kUsed);
		}
		Overlay::Publish(false);
		return;
	}

	Level& lvl = s_stack[s_depth];
	s_select = Input::Pressed() & SCE_PAD_BUTTON_CROSS;
	s_left = Input::Repeat(SCE_PAD_BUTTON_LEFT);
	s_right = Input::Repeat(SCE_PAD_BUTTON_RIGHT) && !(held & SCE_PAD_BUTTON_R3);
	bool up = Input::Repeat(SCE_PAD_BUTTON_UP);
	bool down = Input::Repeat(SCE_PAD_BUTTON_DOWN);
	bool back = Input::Pressed() & SCE_PAD_BUTTON_CIRCLE;

	s_count = 0;
	s_next = nullptr;
	lvl.page();

	if (s_count > 0) {
		if (up) lvl.selected = (lvl.selected - 1 + s_count) % s_count;
		if (down) lvl.selected = (lvl.selected + 1) % s_count;
		if (lvl.selected >= s_count) lvl.selected = s_count - 1;
	}

	if (s_next && s_depth < kMaxDepth - 1) {
		Level& n = s_stack[++s_depth];
		n.page = s_next;
		snprintf(n.title, sizeof(n.title), "%s", s_nextTitle);
		n.selected = 0;
	} else if (back) {
		if (s_depth > 0) s_depth--;
		else {
			s_open = false;
			Input::Block(0);
		}
	}

	Overlay::Publish(s_open);
}

bool Action(const char* text, bool disabled)
{
	Item* it = Add(text, disabled);
	return it && Current() && s_select && !disabled;
}

bool Toggle(const char* text, bool* value, bool disabled)
{
	Item* it = Add(text, disabled);
	if (!it) return false;
	bool changed = Current() && s_select && !disabled;
	if (changed) *value = !*value;
	snprintf(it->value, sizeof(it->value), "%s", *value ? "ON" : "OFF");
	return changed;
}

bool Int(const char* text, int* value, int min, int max, int step, bool disabled)
{
	Item* it = Add(text, disabled);
	if (!it) return false;
	bool changed = false;
	if (Current() && !disabled) {
		if (s_left)  { *value = *value - step < min ? max : *value - step; changed = true; }
		if (s_right) { *value = *value + step > max ? min : *value + step; changed = true; }
		if (s_select) changed = true;
	}
	snprintf(it->value, sizeof(it->value), "< %d >", *value);
	return changed;
}

void Sub(const char* text, Page page, const char*)
{
	Item* it = Add(text, false);
	if (!it) return;
	snprintf(it->value, sizeof(it->value), ">");
	if (Current() && s_select) { s_next = page; s_nextTitle = text; }
}

void Text(const char* text)
{
	Item* it = Add(text, true);
	if (it) { it->keepCase = true; it->disabled = false; }
}

void Label(const char* text)
{
	Add(text, true);
}

const char* Title() { return s_stack[s_depth].title; }
int ItemCount() { return s_count; }
const Item& GetItem(int i) { return s_items[i]; }
int Selected() { return s_stack[s_depth].selected; }

}
