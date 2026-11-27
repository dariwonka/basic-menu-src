

#include "Overlay.h"
#include "Menu.h"
#include "VideoOut.h"
#include "Font8x8.h"
#include "Log.h"
#include <kernel.h>
#include <video_out.h>
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <emmintrin.h>

namespace Overlay {

namespace {
	// display: 3840x2160 RGBA8, 64 KB tiles of 128x128 pixels
	const uint32_t kBlocksPerRow = 30;
	const uint64_t kBufferBytes = 0x1FE0000;

	inline uint64_t TiledOffset(uint32_t x, uint32_t y)
	{
		uint32_t o =
			((x & 3) << 2) | ((y & 7) << 4) | (((x >> 2) & 1) << 7) |
			((((x >> 3) ^ (y >> 3)) & 1) << 8) | ((((x >> 4) ^ (y >> 4)) & 1) << 9) |
			((((x >> 6) ^ (y >> 5)) & 1) << 10) | ((((x >> 5) ^ (y >> 6)) & 1) << 11) |
			(((y >> 3) & 1) << 12) | (((x >> 4) & 1) << 13) | (((y >> 6) & 1) << 14) |
			(((x >> 6) & 1) << 15);
		uint64_t block = (uint64_t)(y >> 7) * kBlocksPerRow + (x >> 7);
		return (block << 16) | o;
	}

	// used when the menu loads after the game has registered its buffers
	const uint64_t kKnownBuffers[2] = { 0x1055C20000ull, 0x1057C00000ull };
	const int32_t kKnownHandle = 1309671936;

	// layout: x multiple of 4, y multiple of 8 (a 4x8 pixel group is one 128-byte run)
	const int kScale = 4;
	const uint32_t kGlyph = 8 * kScale;
	const uint32_t kRows = 16, kRowH = 56, kHeaderH = 96, kLineH = 8, kFooterH = 64;
	const uint32_t kRowsTop = kHeaderH + kLineH;
	const uint32_t kW = 960, kH = kRowsTop + kRows * kRowH + kFooterH;
	const uint32_t kX = 2720, kY = 304;
	const uint32_t kPadX = 28;
	static_assert(kH % 8 == 0, "panel height");

	inline uint32_t Rgb(uint8_t r, uint8_t g, uint8_t b) { return r | (g << 8) | (b << 16) | 0xFF000000u; }
	const uint32_t kHeaderBg = Rgb(8, 8, 10),   kAccent = Rgb(0, 200, 120);
	const uint32_t kRowBg    = Rgb(22, 22, 26), kRowAlt = Rgb(28, 28, 33);
	const uint32_t kText     = Rgb(235, 235, 235), kTextDim = Rgb(110, 110, 118), kTextSel = Rgb(6, 6, 8);
	const uint32_t kWarn     = Rgb(255, 70, 70), kWarnSel = Rgb(140, 0, 0);

	struct Snapshot {
		bool open;
		char title[48];
		char footer[16];
		int count, selected, first;
		Menu::Item items[kRows];
		uint32_t textLen[kRows], valueLen[kRows], valueX[kRows];
		uint32_t titleLen, footerLen;
	};
	Snapshot s_snap;

	std::atomic<int32_t> s_handle{-1};
	int32_t s_activeHandle = -1;
	uint64_t s_buffers[2] = {};
	bool s_writable[2] = {};
	bool s_ready = false, s_failed = false;
	uint64_t s_lastFlip = 0, s_startMs = 0, s_lastDrawMs = 0;

	uint64_t NowMs() { return sceKernelGetProcessTime() / 1000; }

	inline bool TextAt(const char* s, uint32_t len, uint32_t ox, uint32_t oy, uint32_t px, uint32_t py)
	{
		if (py < oy || py >= oy + kGlyph || px < ox) return false;
		uint32_t ci = (px - ox) / kGlyph;
		if (ci >= len) return false;
		unsigned char ch = (unsigned char)s[ci];
		if (ch > 127) ch = '?';
		uint32_t col = ((px - ox) % kGlyph) / kScale, row = (py - oy) / kScale;
		return font8x8_basic[ch][row] & (1u << col);
	}

	inline uint32_t Pixel(uint32_t x, uint32_t y)
	{
		const Snapshot& s = s_snap;
		if (y < kHeaderH)
			return TextAt(s.title, s.titleLen, kPadX, (kHeaderH - kGlyph) / 2, x, y) ? kText : kHeaderBg;
		if (y < kRowsTop) return kAccent;
		if (y >= kH - kFooterH)
			return TextAt(s.footer, s.footerLen, kPadX, kH - kFooterH + (kFooterH - kGlyph) / 2, x, y) ? kTextDim : kHeaderBg;

		uint32_t r = (y - kRowsTop) / kRowH;
		uint32_t bg = (r & 1) ? kRowAlt : kRowBg;
		if ((int)r >= s.count) return kRowBg;
		const Menu::Item& it = s.items[r];
		bool sel = s.first + (int)r == s.selected;
		if (sel) bg = kAccent;
		uint32_t fg = it.warn ? (sel ? kWarnSel : kWarn) : it.disabled ? kTextDim : (sel ? kTextSel : kText);
		uint32_t ty = kRowsTop + r * kRowH + (kRowH - kGlyph) / 2;
		if (TextAt(it.text, s.textLen[r], kPadX, ty, x, y)) return fg;
		if (s.valueLen[r] && TextAt(it.value, s.valueLen[r], s.valueX[r], ty, x, y)) return fg;
		return bg;
	}

	void Draw(uint64_t buffer)
	{
		uint8_t* dst = (uint8_t*)buffer;
		uint32_t group[32];
		for (uint32_t gy = 0; gy < kH; gy += 8) {
			for (uint32_t gx = 0; gx < kW; gx += 4) {
				for (uint32_t j = 0; j < 8; j++)
					for (uint32_t i = 0; i < 4; i++) group[j * 4 + i] = Pixel(gx + i, gy + j);
				// streaming stores: the display engine reads memory, not the CPU cache
				__m128i* d = (__m128i*)(dst + TiledOffset(kX + gx, kY + gy));
				const __m128i* src = (const __m128i*)group;
				for (int k = 0; k < 8; k++) _mm_stream_si128(d + k, _mm_loadu_si128(src + k));
			}
		}
		_mm_sfence();
	}

	bool Writable(uint64_t addr)
	{
		SceKernelVirtualQueryInfo info;
		memset(&info, 0, sizeof(info));
		return sceKernelVirtualQuery((void*)addr, 0, &info, sizeof(info)) >= 0 && info.isCommitted &&
			(uint64_t)info.start <= addr && (uint64_t)info.end >= addr + kBufferBytes &&
			(info.protection & SCE_KERNEL_PROT_CPU_WRITE);
	}

	void Setup()
	{
		bool any = false;
		for (int i = 0; i < 2; i++) {
			uint64_t a = VideoOut::BufferAddress(i);
			s_buffers[i] = a ? a : kKnownBuffers[i];
			s_writable[i] = Writable(s_buffers[i]);
			any |= s_writable[i];
		}
		if (!any) {
			Log("overlay: no writable display buffer");
			s_failed = true;
			return;
		}
		SceVideoOutFlipStatus st;
		s_activeHandle = s_handle.load();
		if (s_activeHandle < 0 && sceVideoOutGetFlipStatus(kKnownHandle, &st) == 0) s_activeHandle = kKnownHandle;
		Log("overlay: buffers %llx %llx, handle %d", (unsigned long long)s_buffers[0],
			(unsigned long long)s_buffers[1], s_activeHandle);
		s_ready = true;
	}
}

void Start()
{
	s_startMs = NowMs();
}

void Publish(bool open)
{
	Snapshot& s = s_snap;
	s.open = open;
	if (!open) return;
	snprintf(s.title, sizeof(s.title), "%s", Menu::Title());
	int total = Menu::ItemCount();
	s.selected = Menu::Selected();
	s.first = s.selected >= (int)kRows ? s.selected - (int)kRows + 1 : 0;
	int n = total - s.first;
	if (n > (int)kRows) n = kRows;
	if (n < 0) n = 0;
	for (int i = 0; i < n; i++) {
		s.items[i] = Menu::GetItem(s.first + i);
		s.textLen[i] = (uint32_t)strlen(s.items[i].text);
		s.valueLen[i] = (uint32_t)strlen(s.items[i].value);
		s.valueX[i] = kW - kPadX - s.valueLen[i] * kGlyph;
	}
	s.count = n;
	snprintf(s.footer, sizeof(s.footer), "%d/%d", total ? s.selected + 1 : 0, total);
	s.titleLen = (uint32_t)strlen(s.title);
	s.footerLen = (uint32_t)strlen(s.footer);
}

void Poll()
{
	if (s_failed) return;
	if (!s_ready) {
		if (NowMs() - s_startMs < 2000) return;
		Setup();
		if (!s_ready) return;
	}
	if (!s_snap.open) return;

	SceVideoOutFlipStatus st;
	if (s_activeHandle >= 0 && sceVideoOutGetFlipStatus(s_activeHandle, &st) == 0) {
		if (st.count == s_lastFlip) return;
		s_lastFlip = st.count;
		int cur = st.currentBuffer;
		if (cur >= 0 && cur < 2 && s_writable[cur]) Draw(s_buffers[cur]);
	} else if (NowMs() - s_lastDrawMs >= 4) {
		s_lastDrawMs = NowMs();
		for (int i = 0; i < 2; i++) if (s_writable[i]) Draw(s_buffers[i]);
	}
}

void SetVideoHandle(int32_t handle)
{
	if (handle < 0 || s_handle.load() >= 0) return;
	SceVideoOutFlipStatus st;
	if (sceVideoOutGetFlipStatus(handle, &st) != 0) return;
	s_handle.store(handle);
}

}
