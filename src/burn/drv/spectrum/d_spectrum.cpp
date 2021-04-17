// FinalBurn NEO ZX Spectrum driver.  NEO edition!
// media: .tap, .z80 snapshots
// input: kbd, kempston: joy1, sinclair intf.2: joy1 & joy2

#include "tiles_generic.h"
#include "z80_intf.h"
#include "ay8910.h"
#include <math.h>
#include "biquad.h"

#if defined (_MSC_VER)
#define strcasecmp stricmp
#endif

static INT32 SpecMode = 0;
#define SPEC_TAP	(1 << 0)
#define SPEC_Z80	(1 << 1)
#define SPEC_128K   (1 << 2)
#define SPEC_INVES	(1 << 3) // Spanish clone (non-contended ula)
#define SPEC_AY8910	(1 << 4)

static UINT8 SpecInputKbd[0x10][0x05] = {
	{ 0, 0, 0, 0, 0 }, // Shift, Z, X, C, V
	{ 0, 0, 0, 0, 0 }, // A, S, D, F, G
	{ 0, 0, 0, 0, 0 }, // Q, W, E, R, T
	{ 0, 0, 0, 0, 0 }, // 1, 2, 3, 4, 5
	{ 0, 0, 0, 0, 0 }, // 0, 9, 8, 7, 6
	{ 0, 0, 0, 0, 0 }, // P, O, I, U, Y
	{ 0, 0, 0, 0, 0 }, // Enter, L, K, J, H
	{ 0, 0, 0, 0, 0 }, // Space, Sym, M, N, B
	{ 0, 0, 0, 0, 0 }, // Kempston joy1
	{ 0, 0, 0, 0, 0 }, // Sinclair intf.2 joy1
	{ 0, 0, 0, 0, 0 }, // Sinclair intf.2 joy2
	// 128K+ Caps Shift (Secondary Membrane)
	{ 0, 0, 0, 0, 0 }, // Edit, CapsLock, True Vid, Inv Vid, Cursor Left
	{ 0, 0, 0, 0, 0 }, // Del, Graph, Cursor Right, Cursor Up, Cursor Down
	{ 0, 0, 0, 0, 0 }, // Break, Ext Mode
	// 128K+ Symbol Shift (Secondary Membrane)
	{ 0, 0, 0, 0, 0 }, // Quote, Semicolon
	{ 0, 0, 0, 0, 0 }  // Period, Comma
};

static UINT8 SpecDips[1];
static UINT8 SpecInput[0x10];
static UINT8 SpecReset;

static UINT8 *AllMem;
static UINT8 *MemEnd;
static UINT8 *RamStart;
static UINT8 *RamEnd;
static UINT8 *SpecZ80Rom;
static UINT8 *SpecZ80Ram;
static UINT8 *SpecVideoRam;
static UINT8 *SpecSnapshotData;
static INT32  SpecSnapshotDataLen;
static UINT8 *SpecTAP;
static INT32  SpecTAPLen;

static UINT32 *SpecPalette;
static UINT8 SpecRecalc;

static UINT8 ula_attr;
static UINT8 ula_scr;
static UINT8 ula_byte;
static UINT8 ula_border;
static UINT8 ula_flash;
static INT32 ula_last_cyc;
static INT32 nExtraCycles;

static INT16 *Buzzer;

static INT32 SpecRamPage;
static INT32 Spec128kMapper;

static INT32 SpecScanlines;
static INT32 SpecCylesPerScanline;
static INT32 SpecContention;

static INT16 *dacbuf; // for dc offset filter
static INT16 dac_lastin;
static INT16 dac_lastout;

static struct BurnInputInfo SpecInputList[] =
{
	{"P1 Up",			BIT_DIGITAL,	&SpecInputKbd[8][3],	"p1 up"				},
	{"P1 Down",			BIT_DIGITAL,	&SpecInputKbd[8][2],	"p1 down"			},
	{"P1 Left",			BIT_DIGITAL,	&SpecInputKbd[8][1],	"p1 left"			},
	{"P1 Right",		BIT_DIGITAL,	&SpecInputKbd[8][0],	"p1 right"			},
	{"P1 Fire",			BIT_DIGITAL,	&SpecInputKbd[8][4],	"p1 fire 1"			},

	{"P2 Up",			BIT_DIGITAL,	&SpecInputKbd[10][3],	"p2 up"				},
	{"P2 Down",			BIT_DIGITAL,	&SpecInputKbd[10][2],	"p2 down"			},
	{"P2 Left",			BIT_DIGITAL,	&SpecInputKbd[10][0],	"p2 left"			},
	{"P2 Right",		BIT_DIGITAL,	&SpecInputKbd[10][1],	"p2 right"			},
	{"P2 Fire",			BIT_DIGITAL,	&SpecInputKbd[10][4],	"p2 fire 1"			},

	{"ENTER",			BIT_DIGITAL,	&SpecInputKbd[6][0],	"keyb_enter"		},
	{"SPACE",			BIT_DIGITAL,	&SpecInputKbd[7][0],	"keyb_space"		},
	{"CAPS SHIFT",		BIT_DIGITAL,	&SpecInputKbd[0][0],	"keyb_left_shift"	},
	{"SYMBOL SHIFT",	BIT_DIGITAL,	&SpecInputKbd[7][1],	"keyb_right_shift"	},
	{"Q",				BIT_DIGITAL,	&SpecInputKbd[2][0],	"keyb_Q"			},
	{"W",				BIT_DIGITAL,	&SpecInputKbd[2][1],	"keyb_W"			},
	{"E",				BIT_DIGITAL,	&SpecInputKbd[2][2],	"keyb_E"			},
	{"R",				BIT_DIGITAL,	&SpecInputKbd[2][3],	"keyb_R"			},
	{"T",				BIT_DIGITAL,	&SpecInputKbd[2][4],	"keyb_T"			},
	{"Y",				BIT_DIGITAL,	&SpecInputKbd[5][4],	"keyb_Y"			},
	{"U",				BIT_DIGITAL,	&SpecInputKbd[5][3],	"keyb_U"			},
	{"I",				BIT_DIGITAL,	&SpecInputKbd[5][2],	"keyb_I"			},
	{"O",				BIT_DIGITAL,	&SpecInputKbd[5][1],	"keyb_O"			},
	{"P",				BIT_DIGITAL,	&SpecInputKbd[5][0],	"keyb_P"			},
	{"A",				BIT_DIGITAL,	&SpecInputKbd[1][0],	"keyb_A"			},
	{"S",				BIT_DIGITAL,	&SpecInputKbd[1][1],	"keyb_S"			},
	{"D",				BIT_DIGITAL,	&SpecInputKbd[1][2],	"keyb_D"			},
	{"F",				BIT_DIGITAL,	&SpecInputKbd[1][3],	"keyb_F"			},
	{"G",				BIT_DIGITAL,	&SpecInputKbd[1][4],	"keyb_G"			},
	{"H",				BIT_DIGITAL,	&SpecInputKbd[6][4],	"keyb_H"			},
	{"J",				BIT_DIGITAL,	&SpecInputKbd[6][3],	"keyb_J"			},
	{"K",				BIT_DIGITAL,	&SpecInputKbd[6][2],	"keyb_K"			},
	{"L",				BIT_DIGITAL,	&SpecInputKbd[6][1],	"keyb_L"			},
	{"Z",				BIT_DIGITAL,	&SpecInputKbd[0][1],	"keyb_Z"			},
	{"X",				BIT_DIGITAL,	&SpecInputKbd[0][2],	"keyb_X"			},
	{"C",				BIT_DIGITAL,	&SpecInputKbd[0][3],	"keyb_C"			},
	{"V",				BIT_DIGITAL,	&SpecInputKbd[0][4],	"keyb_V"			},
	{"B",				BIT_DIGITAL,	&SpecInputKbd[7][4],	"keyb_B"			},
	{"N",				BIT_DIGITAL,	&SpecInputKbd[7][3],	"keyb_N"			},
	{"M",				BIT_DIGITAL,	&SpecInputKbd[7][2],	"keyb_M"			},
	{"1",				BIT_DIGITAL,	&SpecInputKbd[3][0],	"keyb_1"			},
	{"2",				BIT_DIGITAL,	&SpecInputKbd[3][1],	"keyb_2"			},
	{"3",				BIT_DIGITAL,	&SpecInputKbd[3][2],	"keyb_3"			},
	{"4",				BIT_DIGITAL,	&SpecInputKbd[3][3],	"keyb_4"			},
	{"5",				BIT_DIGITAL,	&SpecInputKbd[3][4],	"keyb_5"			},
	{"6",				BIT_DIGITAL,	&SpecInputKbd[4][4],	"keyb_6"			},
	{"7",				BIT_DIGITAL,	&SpecInputKbd[4][3],	"keyb_7"			},
	{"8",				BIT_DIGITAL,	&SpecInputKbd[4][2],	"keyb_8"			},
	{"9",				BIT_DIGITAL,	&SpecInputKbd[4][1],	"keyb_9"			},
	{"0",				BIT_DIGITAL,	&SpecInputKbd[4][0],	"keyb_0"			},

	{"EDIT",			BIT_DIGITAL,	&SpecInputKbd[11][0],	"keyb_insert"		},
	{"CAPS LOCK",		BIT_DIGITAL,	&SpecInputKbd[11][1],	"keyb_caps_lock"	},
	{"TRUE VID",		BIT_DIGITAL,	&SpecInputKbd[11][2],	"keyb_home"			},
	{"INV VID",			BIT_DIGITAL,	&SpecInputKbd[11][3],	"keyb_end"			},

	{"DEL",				BIT_DIGITAL,	&SpecInputKbd[12][0],	"keyb_backspace"	},
	{"GRAPH",			BIT_DIGITAL,	&SpecInputKbd[12][1],	"keyb_left_alt"		},

	{"Cursor Up",		BIT_DIGITAL,	&SpecInputKbd[12][3],	"keyb_cursor_up"	}, // don't auto-map these cursors by default
	{"Cursor Down",		BIT_DIGITAL,	&SpecInputKbd[12][4],	"keyb_cursor_down"	}, // causes trouble w/games when using cursors
	{"Cursor Left",		BIT_DIGITAL,	&SpecInputKbd[11][4],	"keyb_cursor_left"	}, // as joystick! (yie ar kungfu, ...)
	{"Cursor Right",	BIT_DIGITAL,	&SpecInputKbd[12][2],	"keyb_cursor_right"	},

	{"BREAK",			BIT_DIGITAL,	&SpecInputKbd[13][0],	"keyb_pause"		},
	{"EXT MODE",		BIT_DIGITAL,	&SpecInputKbd[13][1],	"keyb_left_ctrl"	},

	{"\"",				BIT_DIGITAL,	&SpecInputKbd[14][0],	"keyb_apost"		},
	{";",				BIT_DIGITAL,	&SpecInputKbd[14][1],	"keyb_colon"		},

	{".",				BIT_DIGITAL,	&SpecInputKbd[15][2],	"keyb_stop"			},
	{",",				BIT_DIGITAL,	&SpecInputKbd[15][3],	"keyb_comma"		},

	{"Reset",			BIT_DIGITAL,	&SpecReset,				"reset"				},
	{"Dip A",			BIT_DIPSWITCH,	SpecDips + 0,			"dip"				},
};

STDINPUTINFO(Spec)

static struct BurnDIPInfo SpecDIPList[]=
{
	DIP_OFFSET(0x43)

	{0, 0xfe, 0   , 2   , "Hardware Version"		},
	{0, 0x01, 0x80, 0x00, "Issue 2"					},
	{0, 0x01, 0x80, 0x80, "Issue 3"					},

	{0, 0xfe, 0   , 5   , "Joystick Config"			},
	{0, 0x01, 0x0f, 0x00, "Kempston"				},
	{0, 0x01, 0x0f, 0x01, "Sinclair Interface 2"	},
	{0, 0x01, 0x0f, 0x02, "QAOPM"					},
	{0, 0x01, 0x0f, 0x04, "QAOP Space"				},
	{0, 0x01, 0x0f, 0x08, "Disabled"				},
};

static struct BurnDIPInfo SpecDefaultDIPList[]=
{
	{0, 0xff, 0xff, 0x80, NULL						}, // Issue 3 + Kempston (Blinky's Scary School requires issue 3)
};

static struct BurnDIPInfo SpecIssue2DIPList[]=
{
	{0, 0xff, 0xff, 0x00, NULL						}, // Issue 2 + Kempston (Abu Simbel requires issue 2)
};

static struct BurnDIPInfo SpecIntf2DIPList[]=
{
	{0, 0xff, 0xff, 0x81, NULL						}, // Sinclair Interface 2 (2 Joysticks)
};

static struct BurnDIPInfo SpecQAOPMDIPList[]=
{
	{0, 0xff, 0xff, 0x82, NULL						}, // Kempston mapped to QAOPM (moon ranger)
};

static struct BurnDIPInfo SpecQAOPSpaceDIPList[]=
{
	{0, 0xff, 0xff, 0x84, NULL						}, // Kempston mapped to QAOPSpace (jet pack bob)
};

STDDIPINFOEXT(Spec, SpecDefault, Spec)
STDDIPINFOEXT(SpecIssue2, SpecIssue2, Spec)
STDDIPINFOEXT(SpecIntf2, SpecIntf2, Spec)
STDDIPINFOEXT(SpecQAOPM, SpecQAOPM, Spec)
STDDIPINFOEXT(SpecQAOPSpace, SpecQAOPSpace, Spec)

static void spectrum128_bank(); // forward
static void spectrum_loadz80();

// Spectrum 48k tap-loading robot -dink
static INT32 CASFrameCounter = 0; // for autoloading
static INT32 CASAutoLoadPos = 0;
static INT32 CASAutoLoadTicker = 0;
static void SpecTAPReset(); // forward

static void SpecLoadTAP()
{
	CASAutoLoadPos = 0;
	CASAutoLoadTicker = 0;
	CASFrameCounter = 0;
}

static UINT8* FindInput(char *str)
{
	for (INT32 i = 0; SpecInputList[i].szName != NULL; i++) {
		if (!strcmp(str, SpecInputList[i].szName)) {
			return SpecInputList[i].pVal;
		}
	}
	return NULL;
}

static void SetInput(char *str, INT32 data)
{
	UINT8 *x = FindInput(str);
	if (x) {
		x[0] = data;
	}
}

static void TAPAutoLoadTick()
{
	const UINT8 TAPLoader[2][10] = { { "J\"\"\n\0" }, { "\n\0" } }; // 48k, 128k
	const INT32 KeyDelay = 12; // frames 0-4: press key, 5-11: delay, 11: next character.

	if (CASAutoLoadPos == 0xff) return;

	UINT8 c = TAPLoader[((SpecMode & SPEC_128K) ? 1 : 0)][CASAutoLoadPos];
	if (!c) {
		CASAutoLoadPos = 0xff;
		return;
	}

	if ((CASAutoLoadTicker % KeyDelay) < 5) {
		switch (c) {
			case '\"': {
				SetInput("SYMBOL SHIFT", 1);
				SetInput("P", 1);
				break;
			}
			case 'J': {
				SetInput("J", 1);
				break;
			}
			case '\n': {
				SetInput("ENTER", 1);
				break;
			}
		}
	}

	if ((CASAutoLoadTicker % KeyDelay) == KeyDelay - 1) CASAutoLoadPos++;
	CASAutoLoadTicker++;
}

static void TAPAutoLoadRobot()
{
	if (SpecMode & SPEC_TAP && CASFrameCounter > 90) {
		TAPAutoLoadTick();
	}
	CASFrameCounter++;
}
// End TAP Robot

// Oversampling Buzzer-DAC
static INT32 buzzer_last_update;
static INT32 buzzer_last_data;
static INT32 buzzer_data_len;
static INT32 buzzer_data_frame;
static INT32 buzzer_data_frame_minute;

static const INT32 buzzer_oversample = 3000;

static BIQ biquad[2]; // snd/biquad.h

static void BuzzerInit() // keep in DoReset()!
{
	biquad[0].init(FILT_LOWPASS, nBurnSoundRate, 7000, 0.554, 0.0);
	biquad[1].init(FILT_LOWPASS, nBurnSoundRate, 8000, 0.554, 0.0);

	buzzer_data_frame_minute = (SpecCylesPerScanline * SpecScanlines * 50.00);
	buzzer_data_frame = ((double)(SpecCylesPerScanline * SpecScanlines) * nBurnSoundRate * buzzer_oversample) / buzzer_data_frame_minute;
}

static void BuzzerExit()
{
	biquad[0].exit();
	biquad[1].exit();
}

static void BuzzerAdd(INT16 data)
{
	data *= (1 << 12);

	if (data != buzzer_last_data) {
		INT32 len = ((double)(ZetTotalCycles() - buzzer_last_update) * nBurnSoundRate * buzzer_oversample) / buzzer_data_frame_minute;
		if (len > 0)
		{
			for (INT32 i = buzzer_data_len; i < buzzer_data_len+len; i++) {
				// if len goes over buzzer_data_frame, wrap around to the beginning.
				Buzzer[i % buzzer_data_frame] = buzzer_last_data;
			}
			buzzer_data_len += len;
		}

		buzzer_last_data = data;
		buzzer_last_update = ZetTotalCycles();
	}
}

static void BuzzerRender(INT16 *dest)
{
	INT32 buzzer_data_pos = 0;

	// fill buffer (if needed)
	if (buzzer_data_len < buzzer_data_frame) {
		for (INT32 i = buzzer_data_len; i < buzzer_data_frame; i++) {
			Buzzer[i] = buzzer_last_data;
		}
		buzzer_data_len = buzzer_data_frame;
	}

	// average + mixdown
	for (INT32 i = 0; i < nBurnSoundLen; i++) {
		INT32 sample = 0;
		for (INT32 j = 0; j < buzzer_oversample; j++) {
			sample += Buzzer[buzzer_data_pos++];
		}
		sample = (INT32)(biquad[1].filter(biquad[0].filter((double)sample / buzzer_oversample)));
		dest[0] = BURN_SND_CLIP(sample);
		dest[1] = BURN_SND_CLIP(sample);
		dest += 2;
	}

	buzzer_data_len = 0;
	buzzer_last_update = 0;
}

// end Oversampling Buzzer-DAC

static INT32 MemIndex()
{
	UINT8 *Next; Next = AllMem;

	SpecZ80Rom              = Next; Next += 0x08000;
	SpecSnapshotData        = Next; Next += 0x20000;
	SpecTAP                 = Next; Next += 0x800000;

	RamStart                = Next;
	SpecZ80Ram              = Next; Next += 0x20000;
	RamEnd                  = Next;

	SpecPalette             = (UINT32*)Next; Next += 0x00010 * sizeof(UINT32);
	dacbuf                  = (INT16*)Next; Next += 0x800 * 2 * sizeof(INT16);

	Buzzer                  = (INT16*)Next; Next += 1000 * buzzer_oversample * sizeof(INT16);

	MemEnd                  = Next;

	return 0;
}

static INT32 SpecDoReset()
{
	ZetOpen(0);
	ZetReset();
	if (SpecMode & SPEC_AY8910) {
		AY8910Reset(0);
	}
	ZetClose();

	BuzzerInit();

	SpecVideoRam = SpecZ80Ram;
	Spec128kMapper = 0;
	ula_border = 0;

	if (SpecMode & SPEC_128K) {
		ZetOpen(0);
		spectrum128_bank();
		ZetClose();
	}

	if (SpecMode & SPEC_Z80) {
		spectrum_loadz80();
	}

	if (SpecMode & SPEC_TAP) {
		SpecLoadTAP(); // reset robot
		SpecTAPReset(); // reset tap engine
	}

	dac_lastin = 0;
	dac_lastout = 0;

	ula_last_cyc = 0;
	ula_byte = 0xff;
	ula_attr = 0x00;
	ula_scr = 0x00;

	nExtraCycles = 0;

	return 0;
}

static UINT8 __fastcall SpecZ80Read(UINT16 address)
{
	if (address < 0x4000) {
		return SpecZ80Rom[address];
	}

	if (address >= 0x4000 && address <= 0x7fff) {
		return SpecZ80Ram[address & 0x3fff];
	}

	if (address >= 0x8000 && address <= 0xffff) {
		return SpecZ80Ram[0x4000 + (address & 0x7fff)];
	}

	bprintf(0, _T("mr %x\n"), address);

	return 0;
}

static void __fastcall SpecZ80Write(UINT16 address, UINT8 data)
{
	if (address < 0x4000) return;

	if (address >= 0x4000 && address <= 0x7fff) {
		SpecZ80Ram[address & 0x3fff] = data;
		return;
	}

	if (address >= 0x8000 && address <= 0xffff) {
		SpecZ80Ram[0x4000 + (address & 0x7fff)] = data;
		return;
	}

	bprintf(0, _T("mw %x %x\n"), address, data);
}

// the 128K has a secondary kbd matrix that added more keys to the keyboard
// which were normally accessed by pressing Caps Shift / Symbol Shift.
// When one of those keys are pressed - it basically uses a little hw hack
// to hold the caps/symbol shift button. -dink
static INT32 check_caps_shift()
{
	INT32 ret = 0;
	for (INT32 i = 11; i <= 13; i++) {
		// check if a 2ndary matrix button that needs CAPS Shift has been pressed
		if (SpecInput[i] != 0x1f) ret = 1;
	}
	return ret;
}

static INT32 check_symbol_shift()
{
	INT32 ret = 0;
	for (INT32 i = 14; i <= 15; i++) {
		// check if a 2ndary matrix button that needs Symbol Shift has been pressed
		if (SpecInput[i] != 0x1f) ret = 1;
	}
	return ret;
}

static UINT8 read_keyboard(UINT16 address)
{
	UINT8 keytmp = 0xff;

	for (INT32 i = 0; i < 8; i++) { // process all kbd rows
		if (~address & (1 << (i + 8))) {
			switch (i) {
				case 0:
					keytmp &= SpecInput[i];
					if (check_caps_shift()) keytmp &= ~(1 << 0);
					break;

				case 3:
					keytmp &= SpecInput[i] & SpecInput[11] & SpecInput[10]; // caps shift0, intf2 joy2
					break;

				case 4:
					keytmp &= SpecInput[i] & SpecInput[12] & SpecInput[9]; // caps shift1, intf2 joy1
					break;

				case 5:
					keytmp &= SpecInput[i] & SpecInput[14]; // symbol shift0
					break;

				case 7:
					keytmp &= SpecInput[i] & SpecInput[13] & SpecInput[15]; // caps shift2, symbol shift1
					if (check_symbol_shift()) keytmp &= ~(1 << 1);
					break;

				default:
					keytmp &= SpecInput[i];
					break;
			}
		}
	}

	keytmp |= 0xe0; // default bits set high

	if (SpecDips[0] & 0x80) keytmp ^= 0x40; // Issue2 keyboard

	return keytmp;
}

static UINT8 __fastcall SpecZ80PortRead(UINT16 address)
{
	if (~address & 0x0001) { // keyboard
		return read_keyboard(address);
	}

	if ((address & 0x1f) == 0x1f && (address & 0x20) == 0) {
		// Kempston only pulls A5 low - Kempston RE: https://www.youtube.com/watch?v=4e1MlxPVyD4
		return SpecInput[8]; // kempston (returns 0xff when disabled)
	}

	if ((address & 0xc002) == 0xc000 && (SpecMode & SPEC_AY8910)) {
		return AY8910Read(0);
	}

	return ula_byte; // Floating Memory
}

static void __fastcall SpecZ80PortWrite(UINT16 address, UINT8 data)
{
	address &= 0xff;

	if (~address & 0x0001) {
		BuzzerAdd((data & 0x10) >> 4);

		ula_border = data;
		return;
	}

	if (SpecMode & SPEC_AY8910) {
		switch (address & 0xc002) {
			case 0x8000: AY8910Write(0, 1, data); return;
			case 0xc000: AY8910Write(0, 0, data); return;
		}
	}

	if (address == 0xfd) return; // Ignore (Jetpac writes here due to a bug in the game code, and it's the reason it won't work on 128k)

	bprintf(0, _T("pw %x %x\n"), address, data);
}

static void spectrum128_bank()
{
	SpecVideoRam = SpecZ80Ram + ((5 + ((Spec128kMapper & 0x08) >> 2)) << 14);

	SpecRamPage = Spec128kMapper & 0x07;

	Z80Contention_set_bank(SpecRamPage);
}

static UINT8 __fastcall SpecSpec128Z80Read(UINT16 address)
{
	if (address < 0x4000) {
		return SpecZ80Rom[((Spec128kMapper & 0x10) << 10) + address];
	}

	if (address >= 0x4000 && address <= 0x7fff) {
		return SpecZ80Ram[(5 << 14) + (address & 0x3fff)];
	}

	if (address >= 0x8000 && address <= 0xbfff) {
		return SpecZ80Ram[(2 << 14) + (address & 0x3fff)];
	}

	if (address >= 0xc000 && address <= 0xffff) {
		return SpecZ80Ram[(SpecRamPage << 14) + (address & 0x3fff)];
	}

	bprintf(0, _T("mr %x\n"), address);

	return 0;
}

static void __fastcall SpecSpec128Z80Write(UINT16 address, UINT8 data)
{
	if (address < 0x4000) return; // ROM

	if (address >= 0x4000 && address <= 0x7fff) {
		SpecZ80Ram[(5 << 14) + (address & 0x3fff)] = data;
		return;
	}

	if (address >= 0x8000 && address <= 0xbfff) {
		SpecZ80Ram[(2 << 14) + (address & 0x3fff)] = data;
		return;
	}

	if (address >= 0xc000 && address <= 0xffff) {
		SpecZ80Ram[(SpecRamPage << 14) + (address & 0x3fff)] = data;
		return;
	}

	bprintf(0, _T("mw %x %x\n"), address, data);
}

static UINT8 __fastcall SpecSpec128Z80PortRead(UINT16 address)
{
	if (~address & 0x0001) { // keyboard
		return read_keyboard(address);
	}

	if ((address & 0x1f) == 0x1f && (address & 0x20) == 0) {
		// Kempston only pulls A5 low - Kempston RE: https://www.youtube.com/watch?v=4e1MlxPVyD4
		return SpecInput[8]; // kempston (returns 0xff when disabled)
	}

	if ((address & 0xc002) == 0xc000 && (SpecMode & SPEC_AY8910)) {
		return AY8910Read(0);
	}

	if ((address & 0x8002) == 0x0000) {
		// todo: figure out what 0x7ffd / 3ffd read does
		//bprintf(0, _T("reading %x (%x)\n"), address, Spec128kMapper);
	}

	return ula_byte; // Floating Memory
}

static void __fastcall SpecSpec128Z80PortWrite(UINT16 address, UINT8 data)
{
	if (!(address & 0x8002)) {
		//bprintf(0, _T("writing %x  %x\n"), address, data);
		if (Spec128kMapper & 0x20) return; // memory lock-latch enabled

		Spec128kMapper = data;

		spectrum128_bank();
		return;
	}

	if (~address & 0x0001) {
		BuzzerAdd((data & 0x10) >> 4);

		ula_border = data;
		return;
	}

	if (SpecMode & SPEC_AY8910) {
		switch (address & 0xc002) {
			case 0x8000: AY8910Write(0, 1, data); return;
			case 0xc000: AY8910Write(0, 0, data); return;
		}
	}

	if (address == 0xff3b || address == 0xbf3b) return; // ignore (some games check for "ula plus" here)

	bprintf(0, _T("pw %x %x\n"), address, data);
}

// Spectrum TAP loader (c) 2020 dink
#define DEBUG_TAP 0
#define BLKNUM 0x200
static UINT8 *SpecTAPBlock[BLKNUM];
static INT32 SpecTAPBlockLen[BLKNUM];

static INT32 SpecTAPBlocks = 0; // 1-based
static INT32 SpecTAPBlocknum = 0; // 0-based
static INT32 SpecTAPPos = 0; // 0-based
static INT32 SpecTAPLoading = 0;

static void SpecTAPReset()
{
	SpecTAPBlocknum = 0;
	SpecTAPPos = 0;
	SpecTAPLoading = 0;
}

static void SpecTAPInit()
{
	for (INT32 i = 0; i < BLKNUM; i++) {
		SpecTAPBlock[i] = NULL;
		SpecTAPBlockLen[i] = 0;
	}
	SpecTAPBlocks = 0;
	SpecTAPBlocknum = 0;
	if (DEBUG_TAP) {
		bprintf(0, _T("**  - Spectrum TAP Loader -\n"));
		bprintf(0, _T("Block#\tLength\tOffset\n"));
	}
	for (INT32 i = 0; i < SpecTAPLen;) {
		INT32 block_size = SpecTAP[i+0] | (SpecTAP[i+1] << 8);

		if (block_size) {
			if (DEBUG_TAP) {
				bprintf(0, _T("%x\t%d\t%x\n"), SpecTAPBlocks, block_size, i+2);
			}

			SpecTAPBlock[SpecTAPBlocks] = &SpecTAP[i+2];
			SpecTAPBlockLen[SpecTAPBlocks] = block_size-2;
			SpecTAPBlocks++;
			if (SpecTAPBlocks >= BLKNUM) {
				bprintf(PRINT_ERROR, _T(".TAP Loader: Tape blocks exceeded.\n"));
				break;
			}
		}

		i += block_size + 2;
	}
}

static INT32 SpecTAPDMACallback()
{
	if (~SpecMode & SPEC_TAP || SpecTAPBlocks == 0) return 0;

	UINT8 *data = SpecTAPBlock[SpecTAPBlocknum];

	INT32 transfer_ok = 0;
	INT32 carry_val = 0;
	INT32 checksum = 0;
	INT32 offset = 0;
	UINT8 byte = 0;

	ActiveZ80EXAF();
	INT32 tap_block = data[0];
	INT32 cpu_block = ActiveZ80GetAF() >> 8;
	INT32 address = ActiveZ80GetIX();
	INT32 length = ActiveZ80GetDE();
	INT32 length_unadjusted = length;

	// If anything is changed here, try the following testcases:
	// Chickin Chase, Alter Ego, V

	if (DEBUG_TAP) {
		bprintf(0, _T("TAP blocknum %d\n"), SpecTAPBlocknum);
		bprintf(0, _T("TAP blocklen %d\n"), SpecTAPBlockLen[SpecTAPBlocknum]);
		bprintf(0, _T("TAP blocktype %x\n"), tap_block);
		bprintf(0, _T("CPU blocktype %x\n"), cpu_block);
		bprintf(0, _T("CPU address %x\n"), address);
		bprintf(0, _T("CPU length %x\n"), length);
	}
	if (length > SpecTAPBlockLen[SpecTAPBlocknum]) {
		bprintf(0, _T("CPU Requested length %x > tape block length %x, adjusting.\n"), length, SpecTAPBlockLen[SpecTAPBlocknum]);
		length = SpecTAPBlockLen[SpecTAPBlocknum];
	}
	if (cpu_block == tap_block) { // we found our block! :)
		if (ActiveZ80GetCarry()) {
			if (DEBUG_TAP) {
				bprintf(0, _T("loading data\n"));
			}
			// load
			offset = 0;
			checksum = tap_block;
			while (offset < length) {
				if (offset+1 > SpecTAPBlockLen[SpecTAPBlocknum]) {
					bprintf(0, _T(".TAP Loader: trying to read past block.  offset %x  blocklen %x\n"), offset, SpecTAPBlockLen[SpecTAPBlocknum]);
					carry_val = 0;
					break;
				}
				byte = data[offset+1];
				ZetWriteByte((address + offset) & 0xffff, data[offset+1]);
				checksum ^= data[offset+1];
				offset++;
			}
			if (DEBUG_TAP) {
				bprintf(0, _T("end dma, checksum %x  tap checksum %x\n"), checksum, data[offset+1]);
			}
			carry_val = (checksum == data[offset+1]);
			transfer_ok = 1;
		}
	}

	ActiveZ80SetCarry(carry_val);
	ActiveZ80SetIX((address + offset) & 0xffff);
	if (transfer_ok) ActiveZ80SetDE(length_unadjusted - SpecTAPBlockLen[SpecTAPBlocknum]);
	ActiveZ80SetHL((checksum << 8) | byte);
	ActiveZ80SetPC(0x05e2);

	SpecTAPBlocknum = (SpecTAPBlocknum + 1) % SpecTAPBlocks;

	return 0;
}

static void snapshot_write_ram(UINT32 address, UINT8 data)
{
	if (address >= 0x4000 && address < 0x24000) { // ignore writes to bios area (snapshot)
		SpecZ80Ram[address - 0x4000] = data;
	} else {
		bprintf(PRINT_ERROR, _T(".z80, snapshot_write_ram(%x, %x).\n"));
	}
}

static void z80_rle_decompress(UINT8 *source, UINT32 dest, UINT16 size)
{
	while (size > 0) {
		if (size > 2 && source[0] == 0xed && source[1] == 0xed) {
			UINT8 count = source[2];
			UINT8 data = source[3];

			if (count == 0)	{
				bprintf(PRINT_ERROR, _T(".z80 rle_decompress: zero length rle-block? eek. (bad .z80 file)\n"));
				return;
			}

			if (count > size) {
				bprintf(PRINT_ERROR, _T(".z80 rle_decompress: count > size, eek. (bad .z80 file)\n"));
				count = size;
			}

			for (INT32 i = 0; i < count; i++) {
				snapshot_write_ram(dest, data);
				dest++;
			}

			source += 4;
			size -= count;
		} else {
			snapshot_write_ram(dest, source[0]);

			dest++;
			source++;
			size--;
		}
	}
}

static UINT16 mem2uint16(INT32 posi, INT32 bigendian)
{
	UINT8 *umem = (UINT8*)&SpecSnapshotData[posi];

	if (bigendian) {
		return (umem[0] << 8) + umem[1];
	} else {
		return (umem[1] << 8) + umem[0];
	}
}

static UINT32 page_to_mem(INT32 page, INT32 is128k)
{
	if (is128k) {
		switch (page) {
			case 3: return 0x4000;
			case 4: return 0x8000;
			case 5: return 0xc000;
			case 6: return 0x10000;
			case 7: return 0x14000;
			case 8: return 0x18000;
			case 9: return 0x1c000;
			case 10: return 0x20000; // ram ends at 0x24000 (0-4000 = rom)
			default: return 0x0000;
		}
	} else {
		switch (page) {
			case 4: return 0x8000;
			case 5: return 0xc000;
			case 8: return 0x4000;
			default: return 0x0000;
		}
	}

	return 0x0000;
}

static void spectrum_loadz80()
{
	ZetSetAF(0, mem2uint16(0, 1));
	ZetSetBC(0, mem2uint16(2, 0));
	ZetSetHL(0, mem2uint16(4, 0));
	UINT16 v1PC = mem2uint16(6, 0);
	ZetSetSP(0, mem2uint16(8, 0));
	ZetSetI(0, (SpecSnapshotData[10] & 0xff));
	ZetSetR(0, (SpecSnapshotData[11] & 0x7f) | ((SpecSnapshotData[12] & 0x01) << 7));
	ula_border = (ula_border & 0xf8) | ((SpecSnapshotData[12] & 0x0e) >> 1);
	ZetSetDE(0, mem2uint16(13, 0));
	ZetSetBC2(0, mem2uint16(15, 0));
	ZetSetDE2(0, mem2uint16(17, 0));
	ZetSetHL2(0, mem2uint16(19, 0));
	ZetSetAF2(0, mem2uint16(21, 1));
	ZetSetIY(0, mem2uint16(23, 0));
	ZetSetIX(0, mem2uint16(25, 0));
	ZetSetIFF1(0, (SpecSnapshotData[27]) ? 1 : 0);
	ZetSetIFF2(0, (SpecSnapshotData[28]) ? 1 : 0);
	ZetSetIM(0, (SpecSnapshotData[29] & 0x03));

	if (v1PC != 0) { // version 1 (48k) snapshot
		bprintf(0, _T(".z80 version 1 - 48k snapshot - "));

		ZetSetPC(0, v1PC);

		if (SpecSnapshotData[12] & 0x20) {
			bprintf(0, _T(".z80: rle-compressed\n"));
			z80_rle_decompress(SpecSnapshotData + 30, 0x4000, 0xc000);
		} else {
			bprintf(0, _T(".z80: un-compressed\n")); // testcase: Horace Goes Skiing
			for (INT32 i = 0x4000; i < 0xc000; i++) {
				snapshot_write_ram(i, SpecSnapshotData[30 + (i - 0x4000)]);
			}
		}
	} else {
		INT32 v2_v3_headerlen = mem2uint16(30, 0);
		INT32 hwmode = SpecSnapshotData[34];
		INT32 v2 = (v2_v3_headerlen == 23);
		INT32 is_128k = ((v2) ? (hwmode > 2) : (hwmode > 3));

		bprintf(0, _T(".z80 version %d - "), (v2) ? 2 : 3);
		if (is_128k) {
			bprintf(0, _T("128k\n"));
			if (~SpecMode & SPEC_128K) {
				bprintf(PRINT_ERROR, _T(".z80 Error: loading 128k snapshot on 48k hw!\n"));
				return;
			}
		} else {
			bprintf(0, _T("48k\n"));
		}

		ZetSetPC(0, mem2uint16(32, 0));

		if (SpecMode & SPEC_AY8910 && SpecSnapshotData[37] & (1<<2)) { // AY8910
			bprintf(0, _T(".z80 contains AY8910 registers\n"));
			ZetOpen(0);
			for (INT32 i = 0; i < 0x10; i++) { // write regs
				AY8910Write(0, 0, i);
				AY8910Write(0, 1, SpecSnapshotData[39 + i]);
			}
			AY8910Write(0, 0, SpecSnapshotData[38]); // write latch
			ZetClose();
		}

		INT32 offset = v2_v3_headerlen + 32;
		while (offset < SpecSnapshotDataLen)
		{
			UINT16 length = mem2uint16(offset, 0);
			UINT32 dest = page_to_mem(SpecSnapshotData[offset + 2], is_128k);

			if (length == 0xffff) {
				length = 0x4000;
				bprintf(0, _T(".z80: copying $%x uncompressed bytes to %x\n"), length, dest);
				for (INT32 i = 0; i < length; i++)
					snapshot_write_ram(dest + i, SpecSnapshotData[offset + 3 + i]);
			} else {
				bprintf(0, _T(".z80: decompressing $%x bytes to %x\n"), length, dest);
				z80_rle_decompress(&SpecSnapshotData[offset + 3], dest, 0x4000);
			}

			offset += 3 + length;
		}

		if (is_128k) {
			Spec128kMapper = (SpecSnapshotData[35] & 0xff);
			ZetOpen(0);
			spectrum128_bank();
			ZetClose();
		}
	}
}

static void update_ula(INT32 cycle); // forward
static void ula_init(INT32 scanlines, INT32 cyc_scanline, INT32 contention);

static INT32 BurnGetLength(INT32 rom_index)
{
	struct BurnRomInfo ri = { "", 0, 0, 0 };
	BurnDrvGetRomInfo(&ri, 0);
	return ri.nLen;
}

static INT32 SpectrumInit(INT32 Mode)
{
	SpecMode = Mode;

	BurnSetRefreshRate(50.0);

	BurnAllocMemIndex();

	INT32 nRet = 0;

	if (SpecMode & SPEC_Z80) {
		// Snapshot
		SpecSnapshotDataLen = BurnGetLength(0);
		nRet = BurnLoadRom(SpecSnapshotData + 0x00000, 0, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x00000, 0x80, 1); if (nRet != 0) return 1;
	}
	else if (SpecMode & SPEC_TAP) {
		// TAP
		SpecTAPLen = BurnGetLength(0);
		nRet = BurnLoadRom(SpecTAP + 0x00000, 0, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x00000, 0x80, 1); if (nRet != 0) return 1;

		SpecTAPInit();
	} else {
		// System
		nRet = BurnLoadRom(SpecZ80Rom + 0x00000, 0, 1); if (nRet != 0) return 1;
	}

	ZetInit(0);
	ZetOpen(0);
	ZetSetReadHandler(SpecZ80Read);
	ZetSetWriteHandler(SpecZ80Write);
	ZetSetInHandler(SpecZ80PortRead);
	ZetSetOutHandler(SpecZ80PortWrite);
	if (SpecMode & SPEC_TAP) {
		bprintf(0, _T("**  Spectrum: Using TAP file (len 0x%x) - DMA Loader\n"), SpecTAPLen);
		z80_set_spectrum_tape_callback(SpecTAPDMACallback);
	}
	if (~SpecMode & SPEC_INVES) {
		Z80InitContention(48, &update_ula);
	}
	ZetClose();

	AY8910Init(0, 17734475 / 10, 0);
	AY8910SetAllRoutes(0, 0.20, BURN_SND_ROUTE_BOTH);
	AY8910SetBuffered(ZetTotalCycles, 224*312*50);

	// Init Buzzer (in DoReset!)

	SpecMode |= SPEC_AY8910;

	GenericTilesInit();

	ula_init(312, 224, 14335);

	SpecDoReset();

	return 0;
}

static INT32 Spectrum128Init(INT32 Mode)
{
	SpecMode = Mode;

	BurnSetRefreshRate(50.0);

	BurnAllocMemIndex();

	INT32 nRet = 0;

	if (SpecMode & SPEC_Z80) {
		// Snapshot
		SpecSnapshotDataLen = BurnGetLength(0);
		nRet = BurnLoadRom(SpecSnapshotData + 0x00000, 0, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x00000, 0x80, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x04000, 0x81, 1); if (nRet != 0) return 1;
	}
	else if (SpecMode & SPEC_TAP) {
		// TAP
		SpecTAPLen = BurnGetLength(0);
		nRet = BurnLoadRom(SpecTAP + 0x00000, 0, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x00000, 0x80, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x04000, 0x81, 1); if (nRet != 0) return 1;

		SpecTAPInit();
	} else {
		// System
		nRet = BurnLoadRom(SpecZ80Rom + 0x00000, 0, 1); if (nRet != 0) return 1;
		nRet = BurnLoadRom(SpecZ80Rom + 0x04000, 1, 1); if (nRet != 0) return 1;
	}

	ZetInit(0);
	ZetOpen(0);
	ZetSetReadHandler(SpecSpec128Z80Read);
	ZetSetWriteHandler(SpecSpec128Z80Write);
	ZetSetInHandler(SpecSpec128Z80PortRead);
	ZetSetOutHandler(SpecSpec128Z80PortWrite);
	if (SpecMode & SPEC_TAP) {
		bprintf(0, _T("**  Spectrum 128k: Using TAP file (len 0x%x) - DMA Loader\n"), SpecTAPLen);
		z80_set_spectrum_tape_callback(SpecTAPDMACallback);
	}
	if (~SpecMode & SPEC_INVES) {
		Z80InitContention(128, &update_ula);
	}
	ZetClose();

	AY8910Init(0, 17734475 / 10, 0);
	AY8910SetAllRoutes(0, 0.20, BURN_SND_ROUTE_BOTH);
	AY8910SetBuffered(ZetTotalCycles, 228*311*50);

	// Init Buzzer (in DoReset!)

	SpecMode |= SPEC_AY8910;

	GenericTilesInit();

	ula_init(311, 228, 14361);

	SpecDoReset();

	return 0;
}

static INT32 get_type()
{
	char *rn = NULL;

	if (!BurnDrvGetRomName(&rn, 0, 0))
	{
		INT32 len = strlen(rn);

		if (len > 4) {
			if (!strcasecmp(".z80", rn + (len-4))) {
				return SPEC_Z80;
			}
			if (!strcasecmp(".tap", rn + (len-4))) {
				return SPEC_TAP;
			}
		}
	}

	return 0;
}

static INT32 SpecInit()
{
	return SpectrumInit(get_type());
}

static INT32 Spec128KInit()
{
	return Spectrum128Init(SPEC_128K | get_type());
}

static INT32 Spec128KInvesInit()
{
	return Spectrum128Init(SPEC_128K | SPEC_INVES | get_type());
}

static INT32 SpecExit()
{
	ZetExit();

	if (SpecMode & SPEC_AY8910) AY8910Exit(0);

	BuzzerExit();

	GenericTilesExit();

	BurnFreeMemIndex();

	return 0;
}

static void SpecCalcPalette()
{
	UINT32 spec_pal[0x10] = {
		0x000000, 0x0000bf, 0xbf0000, 0xbf00bf,
		0x00bf00, 0x00bfbf, 0xbfbf00, 0xbfbfbf,
		0x000000, 0x0000ff, 0xff0000, 0xff00ff,
		0x00ff00, 0x00ffff, 0xffff00, 0xffffff
	};

	for (INT32 i = 0; i < 0x10; i++) {
		SpecPalette[i] = BurnHighCol((spec_pal[i] >> 16) & 0xff, (spec_pal[i] >> 8) & 0xff, spec_pal[i] & 0xff, 0);
	}
}

static INT32 SpecDraw()
{
	if (SpecRecalc) {
		SpecCalcPalette();
		SpecRecalc = 0;
	}

	BurnTransferCopy(SpecPalette);

	return 0;
}

// dink's ULA simulator 2000 SSEI (super special edition intense)
static INT32 CONT_OFFSET;
static INT32 CONT_START;
static INT32 CONT_END;
static INT32 BORDER_START;
static INT32 BORDER_END;

static void ula_init(INT32 scanlines, INT32 cyc_scanline, INT32 contention)
{
	SpecScanlines = scanlines;
	SpecCylesPerScanline = cyc_scanline;
	SpecContention = contention;

	CONT_OFFSET = 3;
	CONT_START  = (SpecContention + CONT_OFFSET); // 14335 / 14361
	CONT_END    = (CONT_START + 192 * SpecCylesPerScanline);
	BORDER_START= (SpecMode & SPEC_128K) ? 14368 : 14342;
	BORDER_START-=(SpecCylesPerScanline * 16) + 6; // "+ 6": (center handlebars in paperboy HS screen)
	BORDER_END  = SpecCylesPerScanline * (16+256+16);
}

static void ula_run_cyc(INT32 cyc)
{
	// borders (top + sides + bottom)
	if (cyc >= BORDER_START && cyc <= BORDER_END) {
		INT32 offset = cyc - BORDER_START;
		INT32 x = ((offset) % SpecCylesPerScanline) * 2;
		INT32 y =  (offset) / SpecCylesPerScanline;
		INT32 border = ula_border & 0x07;

		if ((x & 7) == 0) {
			INT32 draw = 0;

			// top border
			if (y >= 0 && y < 16 && x >= 0 && x <= nScreenWidth-8) {
				draw = 1;
			}

			// side borders
			if (y >= 16 && y < 16+192+16 && ((x >= 0 && x < 16) || (x >= 16+256 && x < 16+256+16)) && x <= nScreenWidth-8) {
				draw = 1;
			}

			// bottom border
			if (y >= 16 + 192 && y < 16+192+16 && x >= 0 && x <= nScreenWidth-8) {
				draw = 1;
			}

			if (draw) {
				ula_byte = 0xff;
				for (INT32 xx = x; xx < x + 8; xx++) {
					pTransDraw[y * nScreenWidth + xx] = border;
				}
			}
		}
	}

	// active area
	if (cyc >= CONT_START && cyc <= CONT_END) {
		INT32 offset = cyc - CONT_START;
		INT32 x = ((offset) % SpecCylesPerScanline) * 2;
		INT32 y =  (offset) / SpecCylesPerScanline;

		if (x < 256) {
			switch (offset % 4) {
				case 0:
					ula_scr = SpecVideoRam[((y & 0xc0) << 5) | ((y & 0x38) << 2) | ((y & 7) << 8) | (x >> 3)];
					break;
				case 1:
					ula_attr = SpecVideoRam[0x1800 | ((y & 0xf8) << 2) | (x >> 3)];
					ula_byte = ula_attr;
					break;
				case 3:
					UINT16 *dst = pTransDraw + ((y + 16) * nScreenWidth) + ((x + 16) & ~7);
					UINT8 ink = (ula_attr & 0x07) + ((ula_attr >> 3) & 0x08);
					UINT8 pap = (ula_attr >> 3) & 0x0f;

					if (ula_flash & 0x10 && ula_attr & 0x80) ula_scr = ~ula_scr;

					for (UINT8 b = 0x80; b != 0; b >>= 1) {
						*dst++ = (ula_scr & b) ? ink : pap;
					}
					break;
			}
		}
	} else {
		ula_byte = 0xff;
	}
}

static void update_ula(INT32 cycle)
{
	if (ula_last_cyc > cycle) {
		// next frame! - finish up previous frame & restart
		for (INT32 i = ula_last_cyc; i < BORDER_END; i++) {
			ula_run_cyc(i);
		}
		ula_last_cyc = 0;
	}

	for (INT32 i = ula_last_cyc; i < cycle; i++) {
		ula_run_cyc(i);
	}

	ula_last_cyc = cycle;
}

static void mix_dcblock(INT16 *inbuf, INT16 *outbuf, INT32 sample_nums)
{
	INT16 out;

	for (INT32 sample = 0; sample < sample_nums; sample++)
	{
		INT16 result = inbuf[sample * 2 + 0]; // source sample

		// dc block
		out = result - dac_lastin + 0.998 * dac_lastout;
		dac_lastin = result;
		dac_lastout = out;

		out *= 2.5;

		// add to stream (+include ay if Spec128)
		if (SpecMode & SPEC_128K) {
			outbuf[sample * 2 + 0] = BURN_SND_CLIP(outbuf[sample * 2 + 0] + out);
			outbuf[sample * 2 + 1] = BURN_SND_CLIP(outbuf[sample * 2 + 1] + out);
		} else {
			outbuf[sample * 2 + 0] = BURN_SND_CLIP(out);
			outbuf[sample * 2 + 1] = BURN_SND_CLIP(out);
		}
	}
}

static INT32 SpecFrame()
{
	if (SpecReset) SpecDoReset();

	if (SpecMode & SPEC_TAP) TAPAutoLoadRobot();

	{
		// Init keyboard matrix & 128k secondary matrix (caps/shifts lines) active-low (0x1f)
		SpecInput[0] = SpecInput[1] = SpecInput[2] = SpecInput[3] = SpecInput[4] = SpecInput[5] = SpecInput[6] = SpecInput[7] = 0x1f;
		SpecInput[11] = SpecInput[12] = SpecInput[13] = SpecInput[14] = SpecInput[15] = 0x1f;

		SpecInput[8] = 0x00; // kempston joy (active high)
		SpecInput[9] = SpecInput[10] = 0x1f; // intf2 joy (active low)

		if (SpecDips[0] & 0x01) { // map kempston joy1 to intf2
			SpecInputKbd[9][1] = SpecInputKbd[8][3];
			SpecInputKbd[9][2] = SpecInputKbd[8][2];
			SpecInputKbd[9][4] = SpecInputKbd[8][1];
			SpecInputKbd[9][3] = SpecInputKbd[8][0];
			SpecInputKbd[9][0] = SpecInputKbd[8][4];
		}

		if (SpecDips[0] & (0x02|0x04)) { // map Kempston to QAOPM/QAOP SPACE
			SpecInputKbd[2][0] = SpecInputKbd[8][3]; // Up -> Q
			SpecInputKbd[1][0] = SpecInputKbd[8][2]; // Down -> A
			SpecInputKbd[5][1] = SpecInputKbd[8][1]; // Left -> O
			SpecInputKbd[5][0] = SpecInputKbd[8][0]; // Right -> P

			switch (SpecDips[0] & (0x02|0x04)) {
				case 0x02:
					SpecInputKbd[7][2] = SpecInputKbd[8][4]; // Fire -> M
					break;
				case 0x04:
					SpecInputKbd[7][0] = SpecInputKbd[8][4]; // Fire -> Space
					break;
			}
		}

		for (INT32 i = 0; i < 5; i++) {
			for (INT32 j = 0; j < 0x10; j++) { // 8x5 keyboard matrix
				SpecInput[j] ^= (SpecInputKbd[j][i] & 1) << i;
			}
		}

		// Disable inactive hw
		if ((SpecDips[0] & 0x0f) != 0x00) { // kempston not selected
			SpecInput[8] = 0xff; // kempston joy (active high, none present returns 0xff)
		}
		if ((SpecDips[0] & 0x0f) != 0x01) { // intf2 not selected
			SpecInput[9] = SpecInput[10] = 0x1f; // intf2 joy (active low)
		}
	}

	INT32 nCyclesDo = 0;

	ZetNewFrame();
	ZetOpen(0);
	ZetIdle(nExtraCycles);
	nExtraCycles = 0;

	for (INT32 i = 0; i < SpecScanlines; i++) {
		if (i == 0) {
			ZetSetIRQLine(0, CPU_IRQSTATUS_ACK);
			nCyclesDo += 32;
			ZetRun(32);
			ZetSetIRQLine(0, CPU_IRQSTATUS_NONE);

			nCyclesDo += SpecCylesPerScanline - 32;
			ZetRun(nCyclesDo - ZetTotalCycles());

			ula_flash = (ula_flash + 1) & 0x1f;
		} else {
			nCyclesDo += SpecCylesPerScanline;
			ZetRun(nCyclesDo - ZetTotalCycles());
		}

		if (SpecMode & SPEC_INVES) {
			update_ula(ZetTotalCycles());
		}
	}

	if (pBurnDraw) {
		SpecDraw();
	}

	if (pBurnSoundOut) {
		if (SpecMode & SPEC_AY8910) {
			AY8910Render(pBurnSoundOut, nBurnSoundLen);
		}

		BuzzerRender(dacbuf);
	    mix_dcblock(dacbuf, pBurnSoundOut, nBurnSoundLen);
	}

	INT32 tot_frame = SpecScanlines * SpecCylesPerScanline;
	nExtraCycles = ZetTotalCycles() - tot_frame;

	ZetClose();

	return 0;
}

static INT32 SpecScan(INT32 nAction, INT32* pnMin)
{
	if (pnMin != NULL) {
		*pnMin = 0x029744;
	}

	if (nAction & ACB_MEMORY_RAM) {
		ScanVar(RamStart, RamEnd-RamStart, "All RAM");
	}

	if (nAction & ACB_DRIVER_DATA) {
		ZetScan(nAction);

		if (SpecMode & SPEC_AY8910) {
			AY8910Scan(nAction, pnMin);
		}

		SCAN_VAR(ula_attr);
		SCAN_VAR(ula_scr);
		SCAN_VAR(ula_byte);
		SCAN_VAR(ula_border);
		SCAN_VAR(ula_flash);
		SCAN_VAR(ula_last_cyc);

		SCAN_VAR(Spec128kMapper);

		SCAN_VAR(nExtraCycles);

		if (SpecMode & SPEC_TAP) {
			// .TAP
			SCAN_VAR(SpecTAPBlocknum);
			// .TAP Robot
			SCAN_VAR(CASAutoLoadPos);
			SCAN_VAR(CASAutoLoadTicker);
			SCAN_VAR(CASFrameCounter);
		}
	}

	if (nAction & ACB_WRITE && SpecMode & SPEC_128K) {
		ZetOpen(0);
		spectrum128_bank();
		ZetClose();
	}

	return 0;
}

static INT32 SpectrumGetZipName(char** pszName, UINT32 i)
{
	static char szFilename[MAX_PATH];
	char* pszGameName = NULL;

	if (pszName == NULL) {
		return 1;
	}

	if (i == 0) {
		pszGameName = BurnDrvGetTextA(DRV_NAME);
	} else {
		if (i == 1 && BurnDrvGetTextA(DRV_BOARDROM)) {
			pszGameName = BurnDrvGetTextA(DRV_BOARDROM);
		} else {
			pszGameName = BurnDrvGetTextA(DRV_PARENT);
		}
	}

	if (pszGameName == NULL || i > 2) {
		*pszName = NULL;
		return 1;
	}

	// remove leader
	memset(szFilename, 0, MAX_PATH);
	for (UINT32 j = 0; j < (strlen(pszGameName) - 5); j++) {
		szFilename[j] = pszGameName[j + 5];
	}

	*pszName = szFilename;

	return 0;
}

// BIOS Handling
static struct BurnRomInfo emptyRomDesc[] = {
	{ "", 0, 0, 0 },
};

static struct BurnRomInfo SpectrumRomDesc[] = {
	{ "spectrum.rom",  0x04000, 0xddee531f, BRF_BIOS },
};

STD_ROM_PICK(Spectrum)
STD_ROM_FN(Spectrum)

static struct BurnRomInfo Spec128RomDesc[] = {
	{ "zx128_0.rom",   0x04000, 0xe76799d2, BRF_BIOS },
	{ "zx128_1.rom",   0x04000, 0xb96a36be, BRF_BIOS },
};

STD_ROM_PICK(Spec128)
STD_ROM_FN(Spec128)

struct BurnDriver BurnSpecSpectrumBIOS = {
	"spec_spectrum", NULL, NULL, NULL, "1984",
	"ZX Spectrum\0", "BIOS Only", "Sinclair Research Limited", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_BOARDROM, 0, HARDWARE_SPECTRUM, GBF_BIOS, 0,
	SpectrumGetZipName, SpectrumRomInfo, SpectrumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

struct BurnDriver BurnSpecSpectrum = {
	"spec_spec48k", NULL, NULL, NULL, "1984",
	"ZX Spectrum\0", NULL, "Sinclair Research Limited", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectrumRomInfo, SpectrumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

struct BurnDriver BurnSpecSpec128BIOS = {
	"spec_spec128", NULL, NULL, NULL, "1984",
	"ZX Spectrum 128\0", "BIOS Only", "Sinclair Research Limited", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_BOARDROM, 0, HARDWARE_SPECTRUM, GBF_BIOS, 0,
	SpectrumGetZipName, Spec128RomInfo, Spec128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

struct BurnDriver BurnSpecSpec128 = {
	"spec_spec128k", NULL, NULL, NULL, "1984",
	"ZX Spectrum 128\0", NULL, "Sinclair Research Limited", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec128RomInfo, Spec128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// games

// 180 (48K)

static struct BurnRomInfo Spec180_48RomDesc[] = {
	{ "180 (1986)(Mastertronic Added Dimension).z80", 0x08d96, 0x8cba8fcd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec180_48, Spec180_48, Spectrum)
STD_ROM_FN(Spec180_48)

struct BurnDriver BurnSpec180_48 = {
	"spec_180_48", "spec_180", "spec_spectrum", NULL, "1986",
	"180 (48K)\0", NULL, "Mastertronic Added Dimension", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec180_48RomInfo, Spec180_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 180 (128K)

static struct BurnRomInfo Spec180RomDesc[] = {
	{ "180 (1986)(Mastertronic Added Dimension)[128K].z80", 0x0d536, 0xc4937cba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec180, Spec180, Spec128)
STD_ROM_FN(Spec180)

struct BurnDriver BurnSpec180 = {
	"spec_180", NULL, "spec_spec128", NULL, "1986",
	"180 (128K)\0", NULL, "Mastertronic Added Dimension", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec180RomInfo, Spec180RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 1942 (48K)

static struct BurnRomInfo Spec1942RomDesc[] = {
	{ "1942 (1986)(Elite Systems).z80", 0x08a78, 0x82b77807, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec1942, Spec1942, Spectrum)
STD_ROM_FN(Spec1942)

struct BurnDriver BurnSpec1942 = {
	"spec_1942", NULL, "spec_spectrum", NULL, "1986",
	"1942 (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, Spec1942RomInfo, Spec1942RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 1943 - The Battle of Midway (48K)

static struct BurnRomInfo Spec1943RomDesc[] = {
	{ "1943 - The Battle of Midway (1988)(Go!).z80", 0x0996f, 0xc97f9144, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec1943, Spec1943, Spectrum)
STD_ROM_FN(Spec1943)

struct BurnDriver BurnSpec1943 = {
	"spec_1943", NULL, "spec_spectrum", NULL, "1988",
	"1943 - The Battle of Midway (48K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, Spec1943RomInfo, Spec1943RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 720 Degrees (48K)

static struct BurnRomInfo Spec720degRomDesc[] = {
	{ "720 Degrees (1986)(U.S. Gold).z80", 0x096c6, 0x25a4c45f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec720deg, Spec720deg, Spectrum)
STD_ROM_FN(Spec720deg)

struct BurnDriver BurnSpec720deg = {
	"spec_720deg", NULL, "spec_spectrum", NULL, "1986",
	"720 Degrees (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, Spec720degRomInfo, Spec720degRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Abu Simbel Profanation (Spanish) (48K)

static struct BurnRomInfo SpecabusimprdRomDesc[] = {
	{ "Abu Simbel Profanation (1985)(Dinamic Software)(es).z80", 0x08dbc, 0xa18b280f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specabusimprd, Specabusimprd, Spectrum)
STD_ROM_FN(Specabusimprd)

struct BurnDriver BurnSpecabusimprd = {
	"spec_abusimprd", "spec_abusimpr", "spec_spectrum", NULL, "1985",
	"Abu Simbel Profanation (Spanish) (48K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecabusimprdRomInfo, SpecabusimprdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIssue2DIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Abu Simbel Profanation (48K)

static struct BurnRomInfo SpecabusimprRomDesc[] = {
	{ "Abu Simbel Profanation (1987)(Gremlin Graphics Software)[re-release].z80", 0x09ab5, 0x54092dfb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specabusimpr, Specabusimpr, Spectrum)
STD_ROM_FN(Specabusimpr)

struct BurnDriver BurnSpecabusimpr = {
	"spec_abusimpr", NULL, "spec_spectrum", NULL, "1987",
	"Abu Simbel Profanation (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecabusimprRomInfo, SpecabusimprRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIssue2DIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ace of Aces (128K)

static struct BurnRomInfo SpecaceofacesRomDesc[] = {
	{ "Ace of Aces 128K (1986)(U.S. Gold).z80", 58746, 0x2bfbb756, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaceofaces, Specaceofaces, Spec128)
STD_ROM_FN(Specaceofaces)

struct BurnDriver BurnSpecaceofaces = {
	"spec_aceofaces", NULL, "spec_spec128", NULL, "1986",
	"Ace of Aces (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT | GBF_SIM, 0,
	SpectrumGetZipName, SpecaceofacesRomInfo, SpecaceofacesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Action Fighter (48K)

static struct BurnRomInfo Specafighter_48RomDesc[] = {
	{ "Action Fighter (1989)(Firebird Software).z80", 0x09384, 0xd61df17c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specafighter_48, Specafighter_48, Spectrum)
STD_ROM_FN(Specafighter_48)

struct BurnDriver BurnSpecafighter_48 = {
	"spec_afighter_48", "spec_afighter", "spec_spectrum", NULL, "1989",
	"Action Fighter (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specafighter_48RomInfo, Specafighter_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Action Fighter (128K)

static struct BurnRomInfo SpecafighterRomDesc[] = {
	{ "Action Fighter (1989)(Firebird Software)[128K].z80", 0x0a81a, 0x55f30b2a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specafighter, Specafighter, Spec128)
STD_ROM_FN(Specafighter)

struct BurnDriver BurnSpecafighter = {
	"spec_afighter", NULL, "spec_spec128", NULL, "1989",
	"Action Fighter (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecafighterRomInfo, SpecafighterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Addams Family, The (128K)

static struct BurnRomInfo SpecaddfamthRomDesc[] = {
	{ "Addams Family, The (1991)(Ocean)(128k).z80", 0x1deee, 0xf4d1671a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaddfamth, Specaddfamth, Spec128)
STD_ROM_FN(Specaddfamth)

struct BurnDriver BurnSpecaddfamth = {
	"spec_addfamth", NULL, "spec_spec128", NULL, "1991",
	"Addams Family, The (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecaddfamthRomInfo, SpecaddfamthRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Adidas Championship Football (128K)

static struct BurnRomInfo SpecadichafoRomDesc[] = {
	{ "Adidas Championship Football (1990)(Ocean)[128K].z80", 0x17369, 0x89364845, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specadichafo, Specadichafo, Spec128)
STD_ROM_FN(Specadichafo)

struct BurnDriver BurnSpecadichafo = {
	"spec_adichafo", NULL, "spec_spec128", NULL, "1990",
	"Adidas Championship Football (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecadichafoRomInfo, SpecadichafoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Advanced Lawnmower Simulator II (128K)

static struct BurnRomInfo Specadvlawn2RomDesc[] = {
	{ "Advanced Lawnmower Simulator II (1990)(JA Software)[128K].z80", 0x021e0, 0x9f4e38e2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specadvlawn2, Specadvlawn2, Spec128)
STD_ROM_FN(Specadvlawn2)

struct BurnDriver BurnSpecadvlawn2 = {
	"spec_advlawn2", NULL, "spec_spec128", NULL, "1990",
	"Advanced Lawnmower Simulator II (128K)\0", NULL, "JA Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specadvlawn2RomInfo, Specadvlawn2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Adventures of Buratino, The (128K)

static struct BurnRomInfo SpecburatinoRomDesc[] = {
	{ "Adventures of Buratino, The (1993)(Copper Feet)(128k).z80", 0x096ec, 0x3a0cb189, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specburatino, Specburatino, Spec128)
STD_ROM_FN(Specburatino)

struct BurnDriver BurnSpecburatino = {
	"spec_buratino", NULL, "spec_spec128", NULL, "1993",
	"Adventures of Buratino, The (128K)\0", NULL, "Copper Feet", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecburatinoRomInfo, SpecburatinoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Afterburner (48K)

static struct BurnRomInfo SpecaburnerRomDesc[] = {
	{ "Afterburner (1988)(Activision).z80", 0x0ab31, 0x1d647b67, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaburner, Specaburner, Spectrum)
STD_ROM_FN(Specaburner)

struct BurnDriver BurnSpecaburner = {
	"spec_aburner", "spec_afterburner", "spec_spectrum", NULL, "1988",
	"Afterburner (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecaburnerRomInfo, SpecaburnerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// After the War (Part 1 of 2) (128K)

static struct BurnRomInfo Specafterthewar1RomDesc[] = {
	{ "After the War (1989)(Dinamic Software)(Part 1 of 2)(128k).z80", 0x093b4, 0x9550b0f4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specafterthewar1, Specafterthewar1, Spec128)
STD_ROM_FN(Specafterthewar1)

struct BurnDriver BurnSpecafterthewar1 = {
	"spec_afterthewar1", NULL, "spec_spec128", NULL, "1989",
	"After the War (Part 1 of 2)\0", "Select keyboard to play - game inputs set on QAOP-M", "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Specafterthewar1RomInfo, Specafterthewar1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// After the War (Part 2 of 2) (128K)

static struct BurnRomInfo Specafterthewar2RomDesc[] = {
	{ "After the War (1989)(Dinamic Software)(Part 2 of 2)(128k).z80", 0x09267, 0x8a306e01, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specafterthewar2, Specafterthewar2, Spec128)
STD_ROM_FN(Specafterthewar2)

struct BurnDriver BurnSpecafterthewar2 = {
	"spec_afterthewar2", "spec_afterthewar1", "spec_spec128", NULL, "1989",
	"After the War (Part 2 of 2)\0", "Select keyboard to play - game inputs set on QAOP-M", "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Specafterthewar2RomInfo, Specafterthewar2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Airwolf (48K)

static struct BurnRomInfo SpecairwolfRomDesc[] = {
	{ "Airwolf (1984)(Elite Systems).z80", 0x0af11, 0xf322ce6a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specairwolf, Specairwolf, Spectrum)
STD_ROM_FN(Specairwolf)

struct BurnDriver BurnSpecairwolf = {
	"spec_airwolf", NULL, "spec_spectrum", NULL, "1984",
	"Airwolf (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecairwolfRomInfo, SpecairwolfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Airwolf II (48K)

static struct BurnRomInfo Specairwolf2RomDesc[] = {
	{ "Airwolf II (1987)(Encore).z80", 0x09ae0, 0x8f6671ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specairwolf2, Specairwolf2, Spectrum)
STD_ROM_FN(Specairwolf2)

struct BurnDriver BurnSpecairwolf2 = {
	"spec_airwolf2", NULL, "spec_spectrum", NULL, "1987",
	"Airwolf II (48K)\0", NULL, "Encore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specairwolf2RomInfo, Specairwolf2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alien Syndrome (128K)

static struct BurnRomInfo SpecaliensynRomDesc[] = {
	{ "Alien Syndrome (1987)(ACE Software)[t +2][128K].z80", 0x0f7bc, 0x11c4832e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaliensyn, Specaliensyn, Spec128)
STD_ROM_FN(Specaliensyn)

struct BurnDriver BurnSpecaliensyn = {
	"spec_aliensyn", NULL, "spec_spec128", NULL, "1987",
	"Alien Syndrome (128K)\0", NULL, "ACE Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecaliensynRomInfo, SpecaliensynRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Altered Beast (128K)

static struct BurnRomInfo SpecaltbeastRomDesc[] = {
	{ "Altered Beast (1988)(Activision)[128K].z80", 0x0b8b5, 0x8c27eb15, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaltbeast, Specaltbeast, Spec128)
STD_ROM_FN(Specaltbeast)

struct BurnDriver BurnSpecaltbeast = {
	"spec_altbeast", NULL, "spec_spec128", NULL, "1988",
	"Altered Beast (128K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecaltbeastRomInfo, SpecaltbeastRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Andy Capp (128K)

static struct BurnRomInfo SpecandycappRomDesc[] = {
	{ "Andy Capp (1988)(Mirrorsoft)[128K].z80", 0x0ad39, 0x77f3f490, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specandycapp, Specandycapp, Spec128)
STD_ROM_FN(Specandycapp)

struct BurnDriver BurnSpecandycapp = {
	"spec_andycapp", NULL, "spec_spec128", NULL, "1988",
	"Andy Capp (128K)\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecandycappRomInfo, SpecandycappRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Angel Nieto Pole 500 (128K)

static struct BurnRomInfo SpecangelnietopoleRomDesc[] = {
	{ "Angel Nieto Pole 500 (1990)(Opera Soft)(128k).z80", 0x09453, 0xcbc4919d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specangelnietopole, Specangelnietopole, Spec128)
STD_ROM_FN(Specangelnietopole)

struct BurnDriver BurnSpecangelnietopole = {
	"spec_angelnietopole", NULL, "spec_spec128", NULL, "1990",
	"Angel Nieto Pole 500 (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecangelnietopoleRomInfo, SpecangelnietopoleRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// APB - All Points Bulletin (128K)

static struct BurnRomInfo SpecapbRomDesc[] = {
	{ "APB - All Points Bulletin (1989)(Domark)[128K].z80", 0x13c67, 0x81a2cb39, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specapb, Specapb, Spec128)
STD_ROM_FN(Specapb)

struct BurnDriver BurnSpecapb = {
	"spec_apb", NULL, "spec_spec128", NULL, "1989",
	"APB - All Points Bulletin (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecapbRomInfo, SpecapbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Arkanoid (48K)

static struct BurnRomInfo SpecarkanoidRomDesc[] = {
	{ "Arkanoid (1987)(Imagine Software).z80", 0x08ad3, 0x6fa4f00f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specarkanoid, Specarkanoid, Spectrum)
STD_ROM_FN(Specarkanoid)

struct BurnDriver BurnSpecarkanoid = {
	"spec_arkanoid", NULL, "spec_spectrum", NULL, "1987",
	"Arkanoid (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecarkanoidRomInfo, SpecarkanoidRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Arkanoid II - Revenge of Doh (48K)

static struct BurnRomInfo Specarkanoid2_48RomDesc[] = {
	{ "Arkanoid II - Revenge of Doh (1988)(Imagine Software).z80", 0x0c01e, 0xaa06fc9e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specarkanoid2_48, Specarkanoid2_48, Spectrum)
STD_ROM_FN(Specarkanoid2_48)

struct BurnDriver BurnSpecarkanoid2_48 = {
	"spec_arkanoid2_48", "spec_arkanoid2", "spec_spectrum", NULL, "1988",
	"Arkanoid II - Revenge of Doh (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specarkanoid2_48RomInfo, Specarkanoid2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Arkanoid II - Revenge of Doh (128K)

static struct BurnRomInfo Specarkanoid2RomDesc[] = {
	{ "Arkanoid II - Revenge of Doh (1988)(Imagine Software)[128K].z80", 0x0cd16, 0x0fa0ffb5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specarkanoid2, Specarkanoid2, Spec128)
STD_ROM_FN(Specarkanoid2)

struct BurnDriver BurnSpecarkanoid2 = {
	"spec_arkanoid2", NULL, "spec_spec128", NULL, "1988",
	"Arkanoid II - Revenge of Doh (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specarkanoid2RomInfo, Specarkanoid2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Army Moves (128K)

static struct BurnRomInfo SpecarmymvsRomDesc[] = {
	{ "Army Moves (1986)(Imagine Software)(128k).z80", 0x0f935, 0xc5552b91, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specarmymvs, Specarmymvs, Spec128)
STD_ROM_FN(Specarmymvs)

struct BurnDriver BurnSpecarmymvs = {
	"spec_armymvs", NULL, "spec_spec128", NULL, "1986",
	"Army Moves (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecarmymvsRomInfo, SpecarmymvsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aspar GP Master (Spanish) (128K)

static struct BurnRomInfo SpecaspargpmasRomDesc[] = {
	{ "Aspar GP Master (1988)(Dinamic Software)(es)(128k).z80", 0x09723, 0x03627882, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaspargpmas, Specaspargpmas, Spec128)
STD_ROM_FN(Specaspargpmas)

struct BurnDriver BurnSpecaspargpmas = {
	"spec_aspargpmas", NULL, "spec_spec128", NULL, "1988",
	"Aspar GP Master (Spanish) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecaspargpmasRomInfo, SpecaspargpmasRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Atic Atac (48K)

static struct BurnRomInfo SpecaticatacRomDesc[] = {
	{ "Atic Atac (1983)(Ultimate Play The Game).z80", 0x08994, 0x56520bdf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaticatac, Specaticatac, Spectrum)
STD_ROM_FN(Specaticatac)

struct BurnDriver BurnSpecaticatac = {
	"spec_aticatac", NULL, "spec_spectrum", NULL, "1983",
	"Atic Atac (48K)\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecaticatacRomInfo, SpecaticatacRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// ATV Simulator - All Terrain Vehicle (48K)

static struct BurnRomInfo Specatvsim_48RomDesc[] = {
	{ "ATV Simulator - All Terrain Vehicle (1987)(Codemasters).z80", 0x0a24d, 0x80171771, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specatvsim_48, Specatvsim_48, Spectrum)
STD_ROM_FN(Specatvsim_48)

struct BurnDriver BurnSpecatvsim_48 = {
	"spec_atvsim_48", "spec_atvsim", "spec_spectrum", NULL, "1987",
	"ATV Simulator - All Terrain Vehicle (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specatvsim_48RomInfo, Specatvsim_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// ATV Simulator - All Terrain Vehicle (128K)

static struct BurnRomInfo SpecatvsimRomDesc[] = {
	{ "ATV Simulator - All Terrain Vehicle (1987)(Codemasters)[128K].z80", 0x0a8ef, 0xe1fc4bb9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specatvsim, Specatvsim, Spec128)
STD_ROM_FN(Specatvsim)

struct BurnDriver BurnSpecatvsim = {
	"spec_atvsim", NULL, "spec_spec128", NULL, "1987",
	"ATV Simulator - All Terrain Vehicle (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecatvsimRomInfo, SpecatvsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Auf Wiedersehen Monty (48K)

static struct BurnRomInfo Specaufwiemo_48RomDesc[] = {
	{ "Auf Wiedersehen Monty (1987)(Gremlin Graphics)[a4].z80", 0x0c01e, 0x1851b7fa, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaufwiemo_48, Specaufwiemo_48, Spectrum)
STD_ROM_FN(Specaufwiemo_48)

struct BurnDriver BurnSpecaufwiemo_48 = {
	"spec_aufwiemo_48", "spec_aufwiemo", "spec_spectrum", NULL, "1987",
	"Auf Wiedersehen Monty (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specaufwiemo_48RomInfo, Specaufwiemo_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Auf Wiedersehen Monty (128K)

static struct BurnRomInfo SpecaufwiemoRomDesc[] = {
	{ "Auf Wiedersehen Monty (1987)(Gremlin Graphics)(128k).z80", 0x10eff, 0x49580b2d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specaufwiemo, Specaufwiemo, Spec128)
STD_ROM_FN(Specaufwiemo)

struct BurnDriver BurnSpecaufwiemo = {
	"spec_aufwiemo", NULL, "spec_spec128", NULL, "1987",
	"Auf Wiedersehen Monty (128K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecaufwiemoRomInfo, SpecaufwiemoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Avalon (48K)

static struct BurnRomInfo SpecavalonRomDesc[] = {
	{ "Avalon (1984)(Hewson Consultants).z80", 0x083f2, 0xe65ee95c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specavalon, Specavalon, Spectrum)
STD_ROM_FN(Specavalon)

struct BurnDriver BurnSpecavalon = {
	"spec_avalon", NULL, "spec_spectrum", NULL, "1984",
	"Avalon (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecavalonRomInfo, SpecavalonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Avenger (128K)

static struct BurnRomInfo SpecavengerRomDesc[] = {
	{ "Avenger 128K (1986)(Gremlin Graphics).z80", 45683, 0x8ac4d620, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specavenger, Specavenger, Spec128)
STD_ROM_FN(Specavenger)

struct BurnDriver BurnSpecavenger = {
	"spec_avenger", NULL, "spec_spec128", NULL, "1986",
	"Avenger (128K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_MAZE, 0,
	SpectrumGetZipName, SpecavengerRomInfo, SpecavengerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Back to Skool (48K)

static struct BurnRomInfo SpecbackskooRomDesc[] = {
	{ "Back to Skool (1985)(Microsphere).z80", 0x0afb3, 0x6bf68f3d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbackskoo, Specbackskoo, Spectrum)
STD_ROM_FN(Specbackskoo)

struct BurnDriver BurnSpecbackskoo = {
	"spec_backskoo", NULL, "spec_spectrum", NULL, "1985",
	"Back to Skool (48K)\0", NULL, "Microsphere", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbackskooRomInfo, SpecbackskooRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Back to the Future (48K)

static struct BurnRomInfo SpecbackfutuRomDesc[] = {
	{ "Back to the Future (1985)(Electric Dreams Software).z80", 0x08d76, 0x9d8d8fa7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbackfutu, Specbackfutu, Spectrum)
STD_ROM_FN(Specbackfutu)

struct BurnDriver BurnSpecbackfutu = {
	"spec_backfutu", NULL, "spec_spectrum", NULL, "1985",
	"Back to the Future (48K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbackfutuRomInfo, SpecbackfutuRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Back to the Future II

static struct BurnRomInfo Specbackfut2RomDesc[] = {
	{ "Back to the Future II (1990)(Image Works).tap", 0x39d95, 0x464a3359, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbackfut2, Specbackfut2, Spec128)
STD_ROM_FN(Specbackfut2)

struct BurnDriver BurnSpecbackfut2 = {
	"spec_backfut2", NULL, "spec_spec128", NULL, "1990",
	"Back to the Future II\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbackfut2RomInfo, Specbackfut2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Back to the Future III

static struct BurnRomInfo Specbackfut3RomDesc[] = {
	{ "Back to the Future III (1991)(Image Works).tap", 0x3661a, 0x5de2c954, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbackfut3, Specbackfut3, Spec128)
STD_ROM_FN(Specbackfut3)

struct BurnDriver BurnSpecbackfut3 = {
	"spec_backfut3", NULL, "spec_spec128", NULL, "1991",
	"Back to the Future III\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbackfut3RomInfo, Specbackfut3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Badlands (Domark) (48K)

static struct BurnRomInfo SpecbadlandsRomDesc[] = {
	{ "Badlands (1990)(Domark).z80", 0x08b3e, 0x93b1febc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbadlands, Specbadlands, Spectrum)
STD_ROM_FN(Specbadlands)

struct BurnDriver BurnSpecbadlands = {
	"spec_badlands", NULL, "spec_spectrum", NULL, "1990",
	"Badlands (Domark) (48K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbadlandsRomInfo, SpecbadlandsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Badlands (Erbe Software) (48K)

static struct BurnRomInfo SpecbadlandseRomDesc[] = {
	{ "Badlands (1990)(Erbe Software)[re-release].z80", 0x0c01e, 0x13128782, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbadlandse, Specbadlandse, Spectrum)
STD_ROM_FN(Specbadlandse)

struct BurnDriver BurnSpecbadlandse = {
	"spec_badlandse", "spec_badlands", "spec_spectrum", NULL, "1990",
	"Badlands (Erbe Software) (48K)\0", NULL, "Erbe Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbadlandseRomInfo, SpecbadlandseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Barbarian: The Ultimate Warrior Part 2 - 1 Player (48K)

static struct BurnRomInfo Specbarbply1RomDesc[] = {
	{ "Barbarian - 1 Player (1987)(Palace Software).z80", 0x0a596, 0x5f06bc26, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbarbply1, Specbarbply1, Spectrum)
STD_ROM_FN(Specbarbply1)

struct BurnDriver BurnSpecbarbply1 = {
	"spec_barbply1", "spec_barbply2", "spec_spectrum", NULL, "1987",
	"Barbarian: The Ultimate Warrior Part 2 - 1 Player (48K)\0", NULL, "Palace Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, Specbarbply1RomInfo, Specbarbply1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Barbarian: The Ultimate Warrior Part 1- 2 Players (48K)

static struct BurnRomInfo Specbarbply2RomDesc[] = {
	{ "Barbarian - 2 Players (1987)(Palace Software).z80", 0x0a2b2, 0xa856967f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbarbply2, Specbarbply2, Spectrum)
STD_ROM_FN(Specbarbply2)

struct BurnDriver BurnSpecbarbply2 = {
	"spec_barbply2", NULL, "spec_spectrum", NULL, "1987",
	"Barbarian: The Ultimate Warrior Part 1 - 2 Players (48K)\0", NULL, "Palace Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, Specbarbply2RomInfo, Specbarbply2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Barbarian (Psygnosis) (48K)

static struct BurnRomInfo Specbarbarn_48RomDesc[] = {
	{ "Barbarian (1988)(Melbourne House).z80", 0x090d1, 0xf386a7ff, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbarbarn_48, Specbarbarn_48, Spectrum)
STD_ROM_FN(Specbarbarn_48)

struct BurnDriver BurnSpecbarbarn_48 = {
	"spec_barbarn_48", "spec_barbarn", "spec_spectrum", NULL, "1988",
	"Barbarian (Psygnosis) (48K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT | GBF_RPG, 0,
	SpectrumGetZipName, Specbarbarn_48RomInfo, Specbarbarn_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Barbarian (Psygnosis) (128K)

static struct BurnRomInfo SpecbarbarnRomDesc[] = {
	{ "Barbarian (1988)(Melbourne House)[128K].z80", 0x0a624, 0x4d0607de, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbarbarn, Specbarbarn, Spec128)
STD_ROM_FN(Specbarbarn)

struct BurnDriver BurnSpecbarbarn = {
	"spec_barbarn", NULL, "spec_spec128", NULL, "1988",
	"Barbarian (Psygnosis) (128K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT | GBF_RPG, 0,
	SpectrumGetZipName, SpecbarbarnRomInfo, SpecbarbarnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Barbarian II - The Dungeon of Drax (128K)

static struct BurnRomInfo Specbarbarn2RomDesc[] = {
	{ "Barbarian II - The Dungeon of Drax (1988)(Palace Software)[128K].z80", 0x1ac6c, 0x2215c3b7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbarbarn2, Specbarbarn2, Spec128)
STD_ROM_FN(Specbarbarn2)

struct BurnDriver BurnSpecbarbarn2 = {
	"spec_barbarn2", NULL, "spec_spec128", NULL, "1988",
	"Barbarian II - The Dungeon of Drax (128K)\0", NULL, "Palace Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Specbarbarn2RomInfo, Specbarbarn2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Basil the Great Mouse Detective (48K)

static struct BurnRomInfo SpecbasilmouseRomDesc[] = {
	{ "Basil the Great Mouse Detective 48K (1987)(Gremlin Graphics).z80", 38391, 0xb5e12867, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbasilmouse, Specbasilmouse, Spectrum)
STD_ROM_FN(Specbasilmouse)

struct BurnDriver BurnSpecbasilmouse = {
	"spec_basilmouse", NULL, "spec_spectrum", NULL, "1987",
	"Basil the Great Mouse Detective (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecbasilmouseRomInfo, SpecbasilmouseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batman - The Caped Crusader - Part 1 - A Bird in the Hand (48K)

static struct BurnRomInfo SpecbatmanccRomDesc[] = {
	{ "Batman - The Caped Crusader - Part 1 - A Bird in the Hand (1988)(Ocean).z80", 0x09dc8, 0x2d3ebd28, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbatmancc, Specbatmancc, Spectrum)
STD_ROM_FN(Specbatmancc)

struct BurnDriver BurnSpecbatmancc = {
	"spec_batmancc", NULL, "spec_spectrum", NULL, "1988",
	"Batman - The Caped Crusader - Part 1 - A Bird in the Hand (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbatmanccRomInfo, SpecbatmanccRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batman - The Caped Crusader - Part 2 - A Fete Worse than Death (48K)

static struct BurnRomInfo Specbatmancc2RomDesc[] = {
	{ "Batman - The Caped Crusader - Part 2 - A Fete Worse than Death (1988)(Ocean).z80", 0x0a1fb, 0x9a27094c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbatmancc2, Specbatmancc2, Spectrum)
STD_ROM_FN(Specbatmancc2)

struct BurnDriver BurnSpecbatmancc2 = {
	"spec_batmancc2", "spec_batmancc", "spec_spectrum", NULL, "1988",
	"Batman - The Caped Crusader - Part 2 - A Fete Worse than Death (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbatmancc2RomInfo, Specbatmancc2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batman - The Movie (128K)

static struct BurnRomInfo SpecbatmanmvRomDesc[] = {
	{ "Batman - The Movie (1989)(Ocean)[128K].z80", 0x1b872, 0x17ed3d84, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbatmanmv, Specbatmanmv, Spec128)
STD_ROM_FN(Specbatmanmv)

struct BurnDriver BurnSpecbatmanmv = {
	"spec_batmanmv", NULL, "spec_spec128", NULL, "1989",
	"Batman - The Movie (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbatmanmvRomInfo, SpecbatmanmvRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batman - The Puaj Edition (48K)

static struct BurnRomInfo SpecbatmanpeRomDesc[] = {
	{ "Batman - The Puaj Edition (1989)(Micro House).z80", 0x086f6, 0x1ec85d88, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbatmanpe, Specbatmanpe, Spectrum)
STD_ROM_FN(Specbatmanpe)

struct BurnDriver BurnSpecbatmanpe = {
	"spec_batmanpe", NULL, "spec_spectrum", NULL, "1989",
	"Batman - The Puaj Edition (48K)\0", NULL, "Micro House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbatmanpeRomInfo, SpecbatmanpeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batman (48K)

static struct BurnRomInfo Specbatman_48RomDesc[] = {
	{ "Batman (1986)(Ocean).z80", 0x0a6bb, 0x0f909c21, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbatman_48, Specbatman_48, Spectrum)
STD_ROM_FN(Specbatman_48)

struct BurnDriver BurnSpecbatman_48 = {
	"spec_batman_48", "spec_batman", "spec_spectrum", NULL, "1986",
	"Batman (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbatman_48RomInfo, Specbatman_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batman (128K)

static struct BurnRomInfo SpecbatmanRomDesc[] = {
	{ "Batman (1986)(Ocean)[128K].z80", 0x0b90a, 0x48ec8253, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbatman, Specbatman, Spec128)
STD_ROM_FN(Specbatman)

struct BurnDriver BurnSpecbatman = {
	"spec_batman", NULL, "spec_spec128", NULL, "1986",
	"Batman (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbatmanRomInfo, SpecbatmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Beach Buggy Simulator (48K)

static struct BurnRomInfo SpecbeabugsiRomDesc[] = {
	{ "Beach Buggy Simulator (1988)(Silverbird Software).z80", 0x096de, 0x900b90e9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbeabugsi, Specbeabugsi, Spectrum)
STD_ROM_FN(Specbeabugsi)

struct BurnDriver BurnSpecbeabugsi = {
	"spec_beabugsi", NULL, "spec_spectrum", NULL, "1988",
	"Beach Buggy Simulator (48K)\0", NULL, "Silverbird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbeabugsiRomInfo, SpecbeabugsiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Beyond the Ice Palace (48K)

static struct BurnRomInfo Specbeyicepa_48RomDesc[] = {
	{ "Beyond the Ice Palace (1988)(Elite Systems).z80", 0x09e04, 0xf039a5d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbeyicepa_48, Specbeyicepa_48, Spectrum)
STD_ROM_FN(Specbeyicepa_48)

struct BurnDriver BurnSpecbeyicepa_48 = {
	"spec_beyicepa_48", "spec_beyicepa", "spec_spectrum", NULL, "1988",
	"Beyond the Ice Palace (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbeyicepa_48RomInfo, Specbeyicepa_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Beyond the Ice Palace (128K)

static struct BurnRomInfo SpecbeyicepaRomDesc[] = {
	{ "Beyond the Ice Palace (1988)(Elite Systems)(128k).z80", 0x0bc09, 0x3bb06772, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbeyicepa, Specbeyicepa, Spec128)
STD_ROM_FN(Specbeyicepa)

struct BurnDriver BurnSpecbeyicepa = {
	"spec_beyicepa", NULL, "spec_spec128", NULL, "1988",
	"Beyond the Ice Palace (128K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbeyicepaRomInfo, SpecbeyicepaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bionic Commando (48K)

static struct BurnRomInfo Specbionicc_48RomDesc[] = {
	{ "Bionic Commando (1988)(Go!).z80", 0x09ba6, 0x69a1c19d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbionicc_48, Specbionicc_48, Spectrum)
STD_ROM_FN(Specbionicc_48)

struct BurnDriver BurnSpecbionicc_48 = {
	"spec_bionicc_48", "spec_bionicc", "spec_spectrum", NULL, "1988",
	"Bionic Commando (48K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbionicc_48RomInfo, Specbionicc_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bionic Commando (128K)

static struct BurnRomInfo SpecbioniccRomDesc[] = {
	{ "Bionic Commando (1988)(Go!)[128K].z80", 0x166f5, 0x8eb507eb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbionicc, Specbionicc, Spec128)
STD_ROM_FN(Specbionicc)

struct BurnDriver BurnSpecbionicc = {
	"spec_bionicc", NULL, "spec_spec128", NULL, "1988",
	"Bionic Commando (128K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbioniccRomInfo, SpecbioniccRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Black Tiger (48K)

static struct BurnRomInfo Specblktiger_48RomDesc[] = {
	{ "Black Tiger (1989)(U.S. Gold).z80", 0x086a4, 0x22b42c5d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specblktiger_48, Specblktiger_48, Spectrum)
STD_ROM_FN(Specblktiger_48)

struct BurnDriver BurnSpecblktiger_48 = {
	"spec_blktiger_48", "spec_blktiger", "spec_spectrum", NULL, "1989",
	"Black Tiger (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specblktiger_48RomInfo, Specblktiger_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Black Tiger (128K)

static struct BurnRomInfo SpecblktigerRomDesc[] = {
	{ "Black Tiger (1989)(U.S. Gold)[128K].z80", 0x0a1a1, 0xc9b3da9b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specblktiger, Specblktiger, Spec128)
STD_ROM_FN(Specblktiger)

struct BurnDriver BurnSpecblktiger = {
	"spec_blktiger", NULL, "spec_spec128", NULL, "1989",
	"Black Tiger (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecblktigerRomInfo, SpecblktigerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blinky's Scary School (48K)

static struct BurnRomInfo SpecblinkysRomDesc[] = {
	{ "Blinky's Scary School (1990)(Zeppelin Games).z80", 0x09cdf, 0x65b0cd8e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specblinkys, Specblinkys, Spectrum)
STD_ROM_FN(Specblinkys)

struct BurnDriver BurnSpecblinkys = {
	"spec_blinkys", NULL, "spec_spectrum", NULL, "1990",
	"Blinky's Scary School (48K)\0", NULL, "Zeppelin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecblinkysRomInfo, SpecblinkysRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// BMX Freestyle Simulator (48K)

static struct BurnRomInfo Specbmxfrees_48RomDesc[] = {
	{ "BMX Freestyle Simulator (1989)(Codemasters).z80", 0x09c65, 0x10749c38, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbmxfrees_48, Specbmxfrees_48, Spectrum)
STD_ROM_FN(Specbmxfrees_48)

struct BurnDriver BurnSpecbmxfrees_48 = {
	"spec_bmxfrees_48", "spec_bmxfrees", "spec_spectrum", NULL, "1989",
	"BMX Freestyle Simulator (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbmxfrees_48RomInfo, Specbmxfrees_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// BMX Freestyle Simulator (128K)

static struct BurnRomInfo SpecbmxfreesRomDesc[] = {
	{ "BMX Freestyle Simulator (1989)(Codemasters)[128K].z80", 0x0b736, 0x58d28848, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbmxfrees, Specbmxfrees, Spec128)
STD_ROM_FN(Specbmxfrees)

struct BurnDriver BurnSpecbmxfrees = {
	"spec_bmxfrees", NULL, "spec_spec128", NULL, "1989",
	"BMX Freestyle Simulator (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbmxfreesRomInfo, SpecbmxfreesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bomb Jack (48K)

static struct BurnRomInfo Specbombjack_48RomDesc[] = {
	{ "Bomb Jack (1986)(Elite Systems).z80", 0x0a30a, 0x00e95211, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbombjack_48, Specbombjack_48, Spectrum)
STD_ROM_FN(Specbombjack_48)

struct BurnDriver BurnSpecbombjack_48 = {
	"spec_bombjack_48", "spec_bombjack", "spec_spectrum", NULL, "1986",
	"Bomb Jack (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbombjack_48RomInfo, Specbombjack_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bomb Jack (128K)

static struct BurnRomInfo SpecbombjackRomDesc[] = {
	{ "Bomb Jack (1986)(Elite Systems)[128K].z80", 0x0a345, 0x71a84d84, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbombjack, Specbombjack, Spec128)
STD_ROM_FN(Specbombjack)

struct BurnDriver BurnSpecbombjack = {
	"spec_bombjack", NULL, "spec_spec128", NULL, "1986",
	"Bomb Jack (128K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbombjackRomInfo, SpecbombjackRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bomb Jack II (48K)

static struct BurnRomInfo Specbmbjack2RomDesc[] = {
	{ "Bomb Jack II (1987)(Elite Systems).z80", 0x0b1b2, 0x6327f471, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbmbjack2, Specbmbjack2, Spectrum)
STD_ROM_FN(Specbmbjack2)

struct BurnDriver BurnSpecbmbjack2 = {
	"spec_bmbjack2", NULL, "spec_spectrum", NULL, "1987",
	"Bomb Jack II (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbmbjack2RomInfo, Specbmbjack2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Boulder Dash (48K)

static struct BurnRomInfo SpecbouldashRomDesc[] = {
	{ "Boulder Dash (1984)(Front Runner).z80", 0x04849, 0x5d71133f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbouldash, Specbouldash, Spectrum)
STD_ROM_FN(Specbouldash)

struct BurnDriver BurnSpecbouldash = {
	"spec_bouldash", NULL, "spec_spectrum", NULL, "1984",
	"Boulder Dash (48K)\0", NULL, "Front Runner", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbouldashRomInfo, SpecbouldashRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Boulder Dash II - Rockford's Riot (48K)

static struct BurnRomInfo Specbouldsh2RomDesc[] = {
	{ "Boulder Dash II - Rockford's Riot (1985)(Prism Leisure).z80", 0x057a4, 0xbcab1101, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbouldsh2, Specbouldsh2, Spectrum)
STD_ROM_FN(Specbouldsh2)

struct BurnDriver BurnSpecbouldsh2 = {
	"spec_bouldsh2", NULL, "spec_spectrum", NULL, "1985",
	"Boulder Dash II - Rockford's Riot (48K)\0", NULL, "Prism Leisure", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbouldsh2RomInfo, Specbouldsh2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Boulder Dash III (48K)

static struct BurnRomInfo Specbouldsh3RomDesc[] = {
	{ "Boulder Dash III (1986)(Prism Leisure).z80", 0x04982, 0xb61f2fae, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbouldsh3, Specbouldsh3, Spectrum)
STD_ROM_FN(Specbouldsh3)

struct BurnDriver BurnSpecbouldsh3 = {
	"spec_bouldsh3", NULL, "spec_spectrum", NULL, "1986",
	"Boulder Dash III (48K)\0", NULL, "Prism Leisure", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbouldsh3RomInfo, Specbouldsh3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Boulder Dash V (48K)

static struct BurnRomInfo Specbouldsh4RomDesc[] = {
	{ "Boulder Dash V (1992)(Too Trek Moscow S.N.G.).z80", 0x08721, 0x6e9b68bf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbouldsh4, Specbouldsh4, Spectrum)
STD_ROM_FN(Specbouldsh4)

struct BurnDriver BurnSpecbouldsh4 = {
	"spec_bouldsh4", NULL, "spec_spectrum", NULL, "1992",
	"Boulder Dash V (48K)\0", NULL, "Too Trek Moscow S.N.G.", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbouldsh4RomInfo, Specbouldsh4RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Boulder Dash VI (48K)

static struct BurnRomInfo Specbouldsh5RomDesc[] = {
	{ "Boulder Dash VI (1992)(Too Trek Moscow S.N.G.).z80", 0x08792, 0x6abfc981, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbouldsh5, Specbouldsh5, Spectrum)
STD_ROM_FN(Specbouldsh5)

struct BurnDriver BurnSpecbouldsh5 = {
	"spec_bouldsh5", NULL, "spec_spectrum", NULL, "1992",
	"Boulder Dash VI (48K)\0", NULL, "Too Trek Moscow S.N.G.", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbouldsh5RomInfo, Specbouldsh5RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bounder (128K)

static struct BurnRomInfo SpecbounderRomDesc[] = {
	{ "Bounder (1986)(Gremlin Graphics Software)(128k).z80", 0x0a431, 0x65b1da45, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbounder, Specbounder, Spec128)
STD_ROM_FN(Specbounder)

struct BurnDriver BurnSpecbounder = {
	"spec_bounder", NULL, "spec_spec128", NULL, "1986",
	"Bounder (128K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbounderRomInfo, SpecbounderRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bruce Lee (48K)

static struct BurnRomInfo SpecbruceleeRomDesc[] = {
	{ "Bruce Lee (1984)(U.S. Gold).z80", 0x08ceb, 0x8298df22, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbrucelee, Specbrucelee, Spectrum)
STD_ROM_FN(Specbrucelee)

struct BurnDriver BurnSpecbrucelee = {
	"spec_brucelee", NULL, "spec_spectrum", NULL, "1984",
	"Bruce Lee (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbruceleeRomInfo, SpecbruceleeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Boss, The (48K)

static struct BurnRomInfo SpecbosstheRomDesc[] = {
	{ "Boss, The (1984)(Peaksoft).z80", 0x09035, 0x9b7242b2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbossthe, Specbossthe, Spectrum)
STD_ROM_FN(Specbossthe)

struct BurnDriver BurnSpecbossthe = {
	"spec_bossthe", NULL, "spec_spectrum", NULL, "1984",
	"Boss, The (48K)\0", NULL, "Peaksoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbosstheRomInfo, SpecbosstheRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bubble Bobble - The Adventure (128K)

static struct BurnRomInfo SpecbubothadRomDesc[] = {
	{ "Bubble Bobble - The Adventure (1993)(AP's Adventures)[128K].z80", 0x0d387, 0xcfa03eec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbubothad, Specbubothad, Spec128)
STD_ROM_FN(Specbubothad)

struct BurnDriver BurnSpecbubothad = {
	"spec_bubothad", NULL, "spec_spec128", NULL, "1993",
	"Bubble Bobble - The Adventure (128K)\0", NULL, "AP's Adventures", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbubothadRomInfo, SpecbubothadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bubble Bobble (48K)

static struct BurnRomInfo Specbublbobl_48RomDesc[] = {
	{ "Bubble Bobble (1987)(Firebird Software).z80", 0x09e39, 0x77c240b3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbublbobl_48, Specbublbobl_48, Spectrum)
STD_ROM_FN(Specbublbobl_48)

struct BurnDriver BurnSpecbublbobl_48 = {
	"spec_bublbobl_48", "spec_bublbobl", "spec_spectrum", NULL, "1987",
	"Bubble Bobble (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbublbobl_48RomInfo, Specbublbobl_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bubble Bobble (128K)

static struct BurnRomInfo SpecbublboblRomDesc[] = {
	{ "Bubble Bobble (1987)(Firebird Software)[128K].z80", 0x0b5c8, 0x7919a50e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbublbobl, Specbublbobl, Spec128)
STD_ROM_FN(Specbublbobl)

struct BurnDriver BurnSpecbublbobl = {
	"spec_bublbobl", NULL, "spec_spec128", NULL, "1987",
	"Bubble Bobble (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbublboblRomInfo, SpecbublboblRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bubble Dizzy (48K)

static struct BurnRomInfo Specbubbdizz_48RomDesc[] = {
	{ "Bubble Dizzy (1991)(Codemasters).z80", 0x09214, 0xfa662366, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbubbdizz_48, Specbubbdizz_48, Spectrum)
STD_ROM_FN(Specbubbdizz_48)

struct BurnDriver BurnSpecbubbdizz_48 = {
	"spec_bubbdizz_48", "spec_bubbdizz", "spec_spectrum", NULL, "1991",
	"Bubble Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, Specbubbdizz_48RomInfo, Specbubbdizz_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bubble Dizzy (128K)

static struct BurnRomInfo SpecbubbdizzRomDesc[] = {
	{ "Bubble Dizzy (1991)(Codemasters)[128K].z80", 0x0d9eb, 0xb9086f44, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbubbdizz, Specbubbdizz, Spec128)
STD_ROM_FN(Specbubbdizz)

struct BurnDriver BurnSpecbubbdizz = {
	"spec_bubbdizz", NULL, "spec_spec128", NULL, "1991",
	"Bubble Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecbubbdizzRomInfo, SpecbubbdizzRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Buck Rogers - Planet of Zoon (48K)

static struct BurnRomInfo SpecbuckrogersRomDesc[] = {
	{ "Buck Rogers - Planet of Zoom 48K (1985)(U.S. Gold).z80", 30201, 0xe8b65171, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbuckrogers, Specbuckrogers, Spectrum)
STD_ROM_FN(Specbuckrogers)

struct BurnDriver BurnSpecbuckrogers = {
	"spec_buckrogers", NULL, "spec_spectrum", NULL, "1984",
	"Buck Rogers - Planet of Zoom (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecbuckrogersRomInfo, SpecbuckrogersRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bulls Eye (48K)

static struct BurnRomInfo SpecbulleyeRomDesc[] = {
	{ "Bulls Eye (1984)(Macsen Software).z80", 0x09944, 0xce764dd1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbulleye, Specbulleye, Spectrum)
STD_ROM_FN(Specbulleye)

struct BurnDriver BurnSpecbulleye = {
	"spec_bulleye", NULL, "spec_spectrum", NULL, "1984",
	"Bulls Eye (48K)\0", NULL, "Macsen Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbulleyeRomInfo, SpecbulleyeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bullseye (Mastertronic) (48K)

static struct BurnRomInfo SpecbullseymRomDesc[] = {
	{ "Bullseye (1982)(Mastertronic)[aka Darts].z80", 0x0935f, 0x1caf68ca, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbullseym, Specbullseym, Spectrum)
STD_ROM_FN(Specbullseym)

struct BurnDriver BurnSpecbullseym = {
	"spec_bullseym", NULL, "spec_spectrum", NULL, "1982",
	"Bullseye (Mastertronic) (48K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbullseymRomInfo, SpecbullseymRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bullseye (GSSCGC) (48K)

static struct BurnRomInfo SpecbullseygRomDesc[] = {
	{ "Bullseye (1997)(CSSCGC).z80", 0x00f78, 0xf985d118, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbullseyg, Specbullseyg, Spectrum)
STD_ROM_FN(Specbullseyg)

struct BurnDriver BurnSpecbullseyg = {
	"spec_bullseyg", NULL, "spec_spectrum", NULL, "1997",
	"Bullseye (GSSCGC) (48K)\0", NULL, "CSSCGC", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbullseygRomInfo, SpecbullseygRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cabal (128K)

static struct BurnRomInfo SpeccabalRomDesc[] = {
	{ "Cabal (1988)(Ocean)[128K].z80", 0x1757e, 0x6174d654, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccabal, Speccabal, Spec128)
STD_ROM_FN(Speccabal)

struct BurnDriver BurnSpeccabal = {
	"spec_cabal", NULL, "spec_spec128", NULL, "1988",
	"Cabal (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccabalRomInfo, SpeccabalRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Camelot Warriors (48K)

static struct BurnRomInfo SpeccamelotwRomDesc[] = {
	{ "Camelot Warriors 48K (1986)(Dinamic).z80", 36763, 0xe2c4a072, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccamelotw, Speccamelotw, Spectrum)
STD_ROM_FN(Speccamelotw)

struct BurnDriver BurnSpeccamelotw = {
	"spec_camelotw", NULL, "spec_spectrum", NULL, "1986",
	"Camelot Warriors (48K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeccamelotwRomInfo, SpeccamelotwRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Capitan Trueno (Spanish) (Part 1 of 2) (128K)

static struct BurnRomInfo Speccaptrueno1RomDesc[] = {
	{ "Capitan Trueno (1989)(Dinamic Software)(es)(Part 1 of 2)(128k).z80", 0x09e33, 0x5cb7a3b1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccaptrueno1, Speccaptrueno1, Spec128)
STD_ROM_FN(Speccaptrueno1)

struct BurnDriver BurnSpeccaptrueno1 = {
	"spec_captrueno1", NULL, "spec_spec128", NULL, "1989",
	"Capitan Trueno (Spanish) (Part 1 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Speccaptrueno1RomInfo, Speccaptrueno1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Capitan Trueno (Spanish) (Part 2 of 2) (128K)

static struct BurnRomInfo Speccaptrueno2RomDesc[] = {
	{ "Capitan Trueno (1989)(Dinamic Software)(es)(Part 2 of 2)(128k).z80", 0x096f6, 0x0b381b12, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccaptrueno2, Speccaptrueno2, Spec128)
STD_ROM_FN(Speccaptrueno2)

struct BurnDriver BurnSpeccaptrueno2 = {
	"spec_captrueno2", "spec_captrueno1", "spec_spec128", NULL, "1989",
	"Capitan Trueno (Spanish) (Part 2 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Speccaptrueno2RomInfo, Speccaptrueno2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Carlos Sainz (128K)

static struct BurnRomInfo SpeccarlossainzRomDesc[] = {
	{ "Carlos Sainz (1990)(Zigurat Software)(128k).z80", 0x0a245, 0x608dabf2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccarlossainz, Speccarlossainz, Spec128)
STD_ROM_FN(Speccarlossainz)

struct BurnDriver BurnSpeccarlossainz = {
	"spec_carlossainz", NULL, "spec_spec128", NULL, "1990",
	"Carlos Sainz (128K)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccarlossainzRomInfo, SpeccarlossainzRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cashdash (48K)

static struct BurnRomInfo SpeccashdashRomDesc[] = {
	{ "Cashdash (19xx)(Tynesoft).z80", 0x083cf, 0x8d84e2b6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccashdash, Speccashdash, Spectrum)
STD_ROM_FN(Speccashdash)

struct BurnDriver BurnSpeccashdash = {
	"spec_cashdash", NULL, "spec_spectrum", NULL, "19xx",
	"Cashdash (48K)\0", NULL, "Tynesoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccashdashRomInfo, SpeccashdashRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cauldron (48K)

static struct BurnRomInfo SpeccauldronRomDesc[] = {
	{ "Cauldron (1985)(Palace).z80", 0x09f7b, 0xadea0ad1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccauldron, Speccauldron, Spectrum)
STD_ROM_FN(Speccauldron)

struct BurnDriver BurnSpeccauldron = {
	"spec_cauldron", NULL, "spec_spectrum", NULL, "1985",
	"Cauldron (48K)\0", NULL, "Palace Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccauldronRomInfo, SpeccauldronRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cauldron II - The Pumpkin Strikes Back (48K)

static struct BurnRomInfo Speccauldrn2RomDesc[] = {
	{ "Cauldron II (1986)(Palace).z80", 0x09349, 0xc73307f0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccauldrn2, Speccauldrn2, Spectrum)
STD_ROM_FN(Speccauldrn2)

struct BurnDriver BurnSpeccauldrn2 = {
	"spec_cauldrn2", NULL, "spec_spectrum", NULL, "1986",
	"Cauldron II - The Pumpkin Strikes Back (48K)\0", NULL, "Palace Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speccauldrn2RomInfo, Speccauldrn2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Canyon Warrior (48K)

static struct BurnRomInfo SpeccanywarrRomDesc[] = {
	{ "Canyon Warrior (1989)(Mastertronic Plus).z80", 0x0860a, 0x086df73a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccanywarr, Speccanywarr, Spectrum)
STD_ROM_FN(Speccanywarr)

struct BurnDriver BurnSpeccanywarr = {
	"spec_canywarr", NULL, "spec_spectrum", NULL, "1989",
	"Canyon Warrior (48K)\0", NULL, "Mastertronic Plus", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccanywarrRomInfo, SpeccanywarrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Championship Jet Ski Simulator - Easy (48K)

static struct BurnRomInfo SpecchjesksiRomDesc[] = {
	{ "Championship Jet Ski Simulator - Easy (1989)(Codemasters).z80", 0x08e58, 0xff247fe4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchjesksi, Specchjesksi, Spectrum)
STD_ROM_FN(Specchjesksi)

struct BurnDriver BurnSpecchjesksi = {
	"spec_chjesksi", NULL, "spec_spectrum", NULL, "1989",
	"Championship Jet Ski Simulator - Easy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecchjesksiRomInfo, SpecchjesksiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Championship Jet Ski Simulator - Hard (48K)

static struct BurnRomInfo SpecchjesksihRomDesc[] = {
	{ "Championship Jet Ski Simulator - Hard (1989)(Codemasters).z80", 0x08f27, 0xe54f4b7a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchjesksih, Specchjesksih, Spectrum)
STD_ROM_FN(Specchjesksih)

struct BurnDriver BurnSpecchjesksih = {
	"spec_chjesksih", "spec_chjesksi", "spec_spectrum", NULL, "1989",
	"Championship Jet Ski Simulator - Hard (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecchjesksihRomInfo, SpecchjesksihRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Championship Sprint (48K)

static struct BurnRomInfo SpecchamspriRomDesc[] = {
	{ "Championship Sprint (1988)(Electric Dreams Software).z80", 0x07073, 0xf557d7f7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchamspri, Specchamspri, Spectrum)
STD_ROM_FN(Specchamspri)

struct BurnDriver BurnSpecchamspri = {
	"spec_chamspri", NULL, "spec_spectrum", NULL, "1988",
	"Championship Sprint (48K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecchamspriRomInfo, SpecchamspriRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chase H.Q. (48K)

static struct BurnRomInfo Specchasehq_48RomDesc[] = {
	{ "Chase H.Q. (1989)(Ocean).z80", 0x0a6f7, 0x5e684c1f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchasehq_48, Specchasehq_48, Spectrum)
STD_ROM_FN(Specchasehq_48)

struct BurnDriver BurnSpecchasehq_48 = {
	"spec_chasehq_48", "spec_chasehq", "spec_spectrum", NULL, "1989",
	"Chase H.Q. (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specchasehq_48RomInfo, Specchasehq_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chase H.Q. (128K)

static struct BurnRomInfo SpecchasehqRomDesc[] = {
	{ "Chase H.Q. (1989)(Ocean)[128K].z80", 0x1c04f, 0xbb5ae933, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchasehq, Specchasehq, Spec128)
STD_ROM_FN(Specchasehq)

struct BurnDriver BurnSpecchasehq = {
	"spec_chasehq", NULL, "spec_spec128", NULL, "1989",
	"Chase H.Q. (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecchasehqRomInfo, SpecchasehqRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chase H.Q. II - Special Criminal Investigations (128K)

static struct BurnRomInfo Specchasehq2RomDesc[] = {
	{ "Chase H.Q. II - Special Criminal Investigations (1990)(Ocean)[128K].z80", 0x19035, 0x83ac9fea, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchasehq2, Specchasehq2, Spec128)
STD_ROM_FN(Specchasehq2)

struct BurnDriver BurnSpecchasehq2 = {
	"spec_chasehq2", NULL, "spec_spec128", NULL, "1990",
	"Chase H.Q. II - Special Criminal Investigations (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specchasehq2RomInfo, Specchasehq2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chequered Flag (48K)

static struct BurnRomInfo SpeccheqflagRomDesc[] = {
	{ "Chequered Flag (1982)(Sinclair Research).z80", 0x08e6f, 0xbe6e657f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccheqflag, Speccheqflag, Spectrum)
STD_ROM_FN(Speccheqflag)

struct BurnDriver BurnSpeccheqflag = {
	"spec_cheqflag", NULL, "spec_spectrum", NULL, "1982",
	"Chequered Flag (48K)\0", NULL, "Sinclair Research", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccheqflagRomInfo, SpeccheqflagRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chicago 30's (128K)

static struct BurnRomInfo Specchicago30RomDesc[] = {
	{ "Chicago 30's (1988)(US Gold)(128k).z80", 0x087be, 0x16ef7dd5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchicago30, Specchicago30, Spec128)
STD_ROM_FN(Specchicago30)

struct BurnDriver BurnSpecchicago30 = {
	"spec_chicago30", NULL, "spec_spec128", NULL, "1988",
	"Chicago 30's (128K)\0", NULL, "US Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specchicago30RomInfo, Specchicago30RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chronos - A Tapestry of Time (128K)

static struct BurnRomInfo SpecchronosRomDesc[] = {
	{ "Chronos - A Tapestry of Time (1987)(Mastertronic)(128k).z80", 0x0aa41, 0x3d9dd3bb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchronos, Specchronos, Spec128)
STD_ROM_FN(Specchronos)

struct BurnDriver BurnSpecchronos = {
	"spec_chronos", NULL, "spec_spec128", NULL, "1987",
	"Chronos - A Tapestry of Time (128K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecchronosRomInfo, SpecchronosRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chuckie Egg (48K)

static struct BurnRomInfo SpecchuckeggRomDesc[] = {
	{ "Chuckie Egg (1983)(A & F Software).z80", 0x04c8b, 0xf274304f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchuckegg, Specchuckegg, Spectrum)
STD_ROM_FN(Specchuckegg)

struct BurnDriver BurnSpecchuckegg = {
	"spec_chuckegg", NULL, "spec_spectrum", NULL, "1983",
	"Chuckie Egg (48K)\0", NULL, "A & F Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecchuckeggRomInfo, SpecchuckeggRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chuckie Egg 2 (48K)

static struct BurnRomInfo Specchuckeg2RomDesc[] = {
	{ "Chuckie Egg 2 (1985)(A & F Software).z80", 0x0a5d9, 0xd5aa2184, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specchuckeg2, Specchuckeg2, Spectrum)
STD_ROM_FN(Specchuckeg2)

struct BurnDriver BurnSpecchuckeg2 = {
	"spec_chuckeg2", NULL, "spec_spectrum", NULL, "1985",
	"Chuckie Egg 2 (48K)\0", NULL, "A & F Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specchuckeg2RomInfo, Specchuckeg2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// CJ's Elephant Antics (Trainer)(48K)

static struct BurnRomInfo SpeccjselephRomDesc[] = {
	{ "CJ's Elephant Antics (1991)(Codemasters).z80", 0x09f76, 0xc249538d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccjseleph, Speccjseleph, Spectrum)
STD_ROM_FN(Speccjseleph)

struct BurnDriver BurnSpeccjseleph = {
	"spec_cjseleph", "spec_cjseleph128", "spec_spectrum", NULL, "1991",
	"CJ's Elephant Antics (Trainer)(48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeccjselephRomInfo, SpeccjselephRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// CJ's Elephant Antics (128K)

static struct BurnRomInfo Speccjseleph128RomDesc[] = {
	{ "CJ's Elephant Antics (1991)(Codemasters)(128k).z80", 0x0e72f, 0x2f5ee9e8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccjseleph128, Speccjseleph128, Spec128)
STD_ROM_FN(Speccjseleph128)

struct BurnDriver BurnSpeccjseleph128 = {
	"spec_cjseleph128", NULL, "spec_spec128", NULL, "1991",
	"CJ's Elephant Antics (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Speccjseleph128RomInfo, Speccjseleph128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// CJ In the USA (128K)

static struct BurnRomInfo SpeccjiiiusaRomDesc[] = {
	{ "CJ In the USA (1991)(Codemasters)(128k).tap", 64000, 0x455F660B, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccjiiiusa, Speccjiiiusa, Spec128)
STD_ROM_FN(Speccjiiiusa)

struct BurnDriver BurnSpeccjiiiusa = {
	"spec_cjiiiusa", NULL, "spec_spec128", NULL, "1991",
	"CJ In the USA (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeccjiiiusaRomInfo, SpeccjiiiusaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Coliseum (128K)

static struct BurnRomInfo SpeccoliseumRomDesc[] = {
	{ "Coliseum (1988)(Kixx)(128k).z80", 0x0b4bf, 0x1f0baec8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccoliseum, Speccoliseum, Spec128)
STD_ROM_FN(Speccoliseum)

struct BurnDriver BurnSpeccoliseum = {
	"spec_coliseum", NULL, "spec_spec128", NULL, "1988",
	"Coliseum (128K)\0", NULL, "Kixx", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccoliseumRomInfo, SpeccoliseumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Combat School (48K)

static struct BurnRomInfo Speccschool_48RomDesc[] = {
	{ "Combat School (1987)(Ocean).z80", 0x0a7c9, 0x5b7421a6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccschool_48, Speccschool_48, Spectrum)
STD_ROM_FN(Speccschool_48)

struct BurnDriver BurnSpeccschool_48 = {
	"spec_cschool_48", "spec_cschool", "spec_spectrum", NULL, "1987",
	"Combat School (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speccschool_48RomInfo, Speccschool_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Combat School (128K)

static struct BurnRomInfo SpeccschoolRomDesc[] = {
	{ "Combat School (1987)(Ocean)[128K].z80", 0x19dd1, 0xe66e39b5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccschool, Speccschool, Spec128)
STD_ROM_FN(Speccschool)

struct BurnDriver BurnSpeccschool = {
	"spec_cschool", NULL, "spec_spec128", NULL, "1987",
	"Combat School (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccschoolRomInfo, SpeccschoolRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Commando (fr) (48K)

static struct BurnRomInfo SpeccommandofRomDesc[] = {
	{ "Commando (1984)(Loriciels)(fr).z80", 0x0a352, 0xc2b0cbfe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccommandof, Speccommandof, Spectrum)
STD_ROM_FN(Speccommandof)

struct BurnDriver BurnSpeccommandof = {
	"spec_commandof", "spec_commando", "spec_spectrum", NULL, "1984",
	"Commando (fr) (48K)\0", NULL, "Loriciels", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccommandofRomInfo, SpeccommandofRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Commando (Pocket Money Software) (48K)

static struct BurnRomInfo SpeccommandopRomDesc[] = {
	{ "Commando (1984)(Pocket Money Software).z80", 0x04924, 0xeaac033a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccommandop, Speccommandop, Spectrum)
STD_ROM_FN(Speccommandop)

struct BurnDriver BurnSpeccommandop = {
	"spec_commandop", "spec_commando", "spec_spectrum", NULL, "1984",
	"Commando (Pocket Money Software) (48K)\0", NULL, "Pocket Money Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccommandopRomInfo, SpeccommandopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Commando (Elite Systems) (48K)

static struct BurnRomInfo SpeccommandoRomDesc[] = {
	{ "Commando (1985)(Elite Systems).z80", 0x0a0dc, 0x6cabf85d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccommando, Speccommando, Spectrum)
STD_ROM_FN(Speccommando)

struct BurnDriver BurnSpeccommando = {
	"spec_commando", NULL, "spec_spectrum", NULL, "1985",
	"Commando (Elite Systems) (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccommandoRomInfo, SpeccommandoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Continental Circus (128K)

static struct BurnRomInfo SpeccontcircRomDesc[] = {
	{ "Continental Circus (1989)(Virgin Mastertronic)[128K].z80", 0x0fcb0, 0xf95c5332, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccontcirc, Speccontcirc, Spec128)
STD_ROM_FN(Speccontcirc)

struct BurnDriver BurnSpeccontcirc = {
	"spec_contcirc", NULL, "spec_spec128", NULL, "1989",
	"Continental Circus (128K)\0", NULL, "Virgin Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccontcircRomInfo, SpeccontcircRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Corsarios (Part 1 of 2)

static struct BurnRomInfo Speccorsarios1RomDesc[] = {
	{ "Corsarios (Part 1 of 2) (1989)(Opera Soft)(128k).z80", 0x09dae, 0xb7d7624e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccorsarios1, Speccorsarios1, Spec128)
STD_ROM_FN(Speccorsarios1)

struct BurnDriver BurnSpeccorsarios1 = {
	"spec_corsarios1", NULL, "spec_spec128", NULL, "1989",
	"Corsarios (Part 1 of 2)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Speccorsarios1RomInfo, Speccorsarios1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Corsarios (Part 2 of 2)

static struct BurnRomInfo Speccorsarios2RomDesc[] = {
	{ "Corsarios (Part 2 of 2) (1989)(Opera Soft)(128k).z80", 0x09fa3, 0xceb25c2a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccorsarios2, Speccorsarios2, Spec128)
STD_ROM_FN(Speccorsarios2)

struct BurnDriver BurnSpeccorsarios2 = {
	"spec_corsarios2", "spec_corsarios1", "spec_spec128", NULL, "1989",
	"Corsarios (Part 2 of 2)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Speccorsarios2RomInfo, Speccorsarios2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Crystal Castles (48K)

static struct BurnRomInfo Speccryscast_48RomDesc[] = {
	{ "Crystal Castles (1986)(U.S. Gold).z80", 0x0c01e, 0xff19b90c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccryscast_48, Speccryscast_48, Spectrum)
STD_ROM_FN(Speccryscast_48)

struct BurnDriver BurnSpeccryscast_48 = {
	"spec_cryscast_48", "spec_cryscast", "spec_spectrum", NULL, "1986",
	"Crystal Castles (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speccryscast_48RomInfo, Speccryscast_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Crystal Castles (128K)

static struct BurnRomInfo SpeccryscastRomDesc[] = {
	{ "Crystal Castles (1986)(U.S. Gold)[128K].z80", 0x0a752, 0xff640ca8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccryscast, Speccryscast, Spec128)
STD_ROM_FN(Speccryscast)

struct BurnDriver BurnSpeccryscast = {
	"spec_cryscast", NULL, "spec_spec128", NULL, "1986",
	"Crystal Castles (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccryscastRomInfo, SpeccryscastRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cybernoid - The Fighting Machine (48K)

static struct BurnRomInfo Speccythfima_48RomDesc[] = {
	{ "Cybernoid (1988)(Hewson Consultants).z80", 0x0a4fb, 0x539a8179, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccythfima_48, Speccythfima_48, Spectrum)
STD_ROM_FN(Speccythfima_48)

struct BurnDriver BurnSpeccythfima_48 = {
	"spec_cythfima_48", "spec_cythfima", "spec_spectrum", NULL, "1988",
	"Cybernoid - The Fighting Machine (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speccythfima_48RomInfo, Speccythfima_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cybernoid - The Fighting Machine (128K)

static struct BurnRomInfo SpeccythfimaRomDesc[] = {
	{ "Cybernoid (1988)(Hewson Consultants)(128k).z80", 0x0aac0, 0x4e00cf3d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccythfima, Speccythfima, Spec128)
STD_ROM_FN(Speccythfima)

struct BurnDriver BurnSpeccythfima = {
	"spec_cythfima", NULL, "spec_spec128", NULL, "1988",
	"Cybernoid - The Fighting Machine (128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccythfimaRomInfo, SpeccythfimaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cybernoid II - The Revenge (48K)

static struct BurnRomInfo Speccybrnd2_48RomDesc[] = {
	{ "Cybernoid II (1988)(Hewson Consultants).z80", 0x0a67b, 0xb7018e24, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccybrnd2_48, Speccybrnd2_48, Spectrum)
STD_ROM_FN(Speccybrnd2_48)

struct BurnDriver BurnSpeccybrnd2_48 = {
	"spec_cybrnd2_48", "spec_cybrnd2", "spec_spectrum", NULL, "1988",
	"Cybernoid II - The Revenge (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speccybrnd2_48RomInfo, Speccybrnd2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cybernoid II - The Revenge (128K)

static struct BurnRomInfo Speccybrnd2RomDesc[] = {
	{ "Cybernoid II (1988)(Hewson Consultants)(128k).z80", 0x0bb35, 0x773b8e31, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccybrnd2, Speccybrnd2, Spec128)
STD_ROM_FN(Speccybrnd2)

struct BurnDriver BurnSpeccybrnd2 = {
	"spec_cybrnd2", NULL, "spec_spec128", NULL, "1988",
	"Cybernoid II - The Revenge (128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speccybrnd2RomInfo, Speccybrnd2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Daley Thompson's Decathlon - Day 1 (48K)

static struct BurnRomInfo SpecdtdecthnRomDesc[] = {
	{ "Daley Thompson's Decathlon - Day 1 (1984)(Ocean).z80", 0x08bbe, 0xf31094d1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdtdecthn, Specdtdecthn, Spectrum)
STD_ROM_FN(Specdtdecthn)

struct BurnDriver BurnSpecdtdecthn = {
	"spec_dtdecthn", NULL, "spec_spectrum", NULL, "1984",
	"Daley Thompson's Decathlon - Day 1 (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, SpecdtdecthnRomInfo, SpecdtdecthnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Daley Thompson's Decathlon - Day 2 (48K)

static struct BurnRomInfo Specdtdecthn2RomDesc[] = {
	{ "Daley Thompson's Decathlon - Day 2 (1984)(Ocean).z80", 0x08a49, 0x500ca1a5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdtdecthn2, Specdtdecthn2, Spectrum)
STD_ROM_FN(Specdtdecthn2)

struct BurnDriver BurnSpecdtdecthn2 = {
	"spec_dtdecthn2", "spec_dtdecthn", "spec_spectrum", NULL, "1984",
	"Daley Thompson's Decathlon - Day 2 (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, Specdtdecthn2RomInfo, Specdtdecthn2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Daley Thompson's Olympic Challenge (128K)

static struct BurnRomInfo SpecdatholchRomDesc[] = {
	{ "Daley Thompson's Olympic Challenge (1988)(Ocean)[128K].z80", 0x1d5dc, 0xfc2d513f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdatholch, Specdatholch, Spec128)
STD_ROM_FN(Specdatholch)

struct BurnDriver BurnSpecdatholch = {
	"spec_datholch", NULL, "spec_spec128", NULL, "1988",
	"Daley Thompson's Olympic Challenge (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, SpecdatholchRomInfo, SpecdatholchRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Daley Thompson's Supertest - Day 1 (48K)

static struct BurnRomInfo Specdtsprtst_48RomDesc[] = {
	{ "Daley Thompson's Supertest - Day 1 (1985)(Ocean).z80", 0x09808, 0xe68ca3de, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdtsprtst_48, Specdtsprtst_48, Spectrum)
STD_ROM_FN(Specdtsprtst_48)

struct BurnDriver BurnSpecdtsprtst_48 = {
	"spec_dtsprtst_48", "spec_dtsprtst", "spec_spectrum", NULL, "1985",
	"Daley Thompson's Supertest - Day 1 (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, Specdtsprtst_48RomInfo, Specdtsprtst_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Daley Thompson's Supertest - Day 2 (48K)

static struct BurnRomInfo Specdtsprtst2_48RomDesc[] = {
	{ "Daley Thompson's Supertest - Day 2 (1985)(Ocean).z80", 0x0a230, 0xba051953, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdtsprtst2_48, Specdtsprtst2_48, Spectrum)
STD_ROM_FN(Specdtsprtst2_48)

struct BurnDriver BurnSpecdtsprtst2_48 = {
	"spec_dtsprtst2_48", "spec_dtsprtst", "spec_spectrum", NULL, "1985",
	"Daley Thompson's Supertest - Day 2 (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, Specdtsprtst2_48RomInfo, Specdtsprtst2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Daley Thompson's Supertest (128K)

static struct BurnRomInfo SpecdtsprtstRomDesc[] = {
	{ "Daley Thompson's Supertest (1985)(Ocean)[128K].z80", 0x190ba, 0xc6bbb38c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdtsprtst, Specdtsprtst, Spec128)
STD_ROM_FN(Specdtsprtst)

struct BurnDriver BurnSpecdtsprtst = {
	"spec_dtsprtst", NULL, "spec_spec128", NULL, "1985",
	"Daley Thompson's Supertest (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, SpecdtsprtstRomInfo, SpecdtsprtstRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dan Dare - Pilot of the Future (48K)

static struct BurnRomInfo SpecdandareRomDesc[] = {
	{ "Dan Dare - Pilot of the Future (1986)(Virgin Games).z80", 0x0a5ac, 0x9378e2c3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdandare, Specdandare, Spectrum)
STD_ROM_FN(Specdandare)

struct BurnDriver BurnSpecdandare = {
	"spec_dandare", NULL, "spec_spectrum", NULL, "1986",
	"Dan Dare - Pilot of the Future (48K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdandareRomInfo, SpecdandareRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dan Dare II - Mekon's Revenge (48K)

static struct BurnRomInfo Specdandare2RomDesc[] = {
	{ "Dan Dare II - Mekon's Revenge (1988)(Virgin Games).z80", 0x09f88, 0xcbd5a032, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdandare2, Specdandare2, Spectrum)
STD_ROM_FN(Specdandare2)

struct BurnDriver BurnSpecdandare2 = {
	"spec_dandare2", NULL, "spec_spectrum", NULL, "1988",
	"Dan Dare II - Mekon's Revenge (48K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specdandare2RomInfo, Specdandare2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dan Dare III - The Escape (48K)

static struct BurnRomInfo Specdandare3RomDesc[] = {
	{ "Dan Dare III - The Escape (1990)(Virgin Games).z80", 0x0a6a4, 0xad0767cb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdandare3, Specdandare3, Spectrum)
STD_ROM_FN(Specdandare3)

struct BurnDriver BurnSpecdandare3 = {
	"spec_dandare3", NULL, "spec_spectrum", NULL, "1990",
	"Dan Dare III - The Escape (48K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specdandare3RomInfo, Specdandare3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Darius+

static struct BurnRomInfo SpecdariusRomDesc[] = {
	{ "Darius+ (1990)(The Edge Software).tap", 0x35789, 0xe47f46f1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdarius, Specdarius, Spec128)
STD_ROM_FN(Specdarius)

struct BurnDriver BurnSpecdarius = {
	"spec_darius", NULL, "spec_spec128", NULL, "1990",
	"Darius+\0", NULL, "The Edge Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdariusRomInfo, SpecdariusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Darkman (128K)

static struct BurnRomInfo SpecdarkmanRomDesc[] = {
	{ "Darkman (1991)(Ocean)(128k).z80", 0x1535e, 0x8246bf52, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdarkman, Specdarkman, Spec128)
STD_ROM_FN(Specdarkman)

struct BurnDriver BurnSpecdarkman = {
	"spec_darkman", NULL, "spec_spec128", NULL, "1991",
	"Darkman (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdarkmanRomInfo, SpecdarkmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Deathchase (48K)

static struct BurnRomInfo SpecdeathchaRomDesc[] = {
	{ "Deathchase (1983)(Micromega).z80", 0x02bed, 0x040769a4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdeathcha, Specdeathcha, Spectrum)
STD_ROM_FN(Specdeathcha)

struct BurnDriver BurnSpecdeathcha = {
	"spec_deathcha", NULL, "spec_spectrum", NULL, "1983",
	"Deathchase (48K)\0", NULL, "Micromega", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdeathchaRomInfo, SpecdeathchaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy - Down the Rapids (48K)

static struct BurnRomInfo Specdizdowra_48RomDesc[] = {
	{ "Dizzy - Down the Rapids (1991)(Codemasters).z80", 0x08372, 0xfc6302d6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizdowra_48, Specdizdowra_48, Spectrum)
STD_ROM_FN(Specdizdowra_48)

struct BurnDriver BurnSpecdizdowra_48 = {
	"spec_dizdowra_48", "spec_dizdowra", "spec_spectrum", NULL, "1991",
	"Dizzy - Down the Rapids (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specdizdowra_48RomInfo, Specdizdowra_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy - Down the Rapids (128K)

static struct BurnRomInfo SpecdizdowraRomDesc[] = {
	{ "Dizzy - Down the Rapids (1991)(Codemasters)[128K].z80", 0x0a17e, 0x426abaa3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizdowra, Specdizdowra, Spec128)
STD_ROM_FN(Specdizdowra)

struct BurnDriver BurnSpecdizdowra = {
	"spec_dizdowra", NULL, "spec_spec128", NULL, "1991",
	"Dizzy - Down the Rapids (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdizdowraRomInfo, SpecdizdowraRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy - The Ultimate Cartoon Adventure (Trainer)(Russian)(128K)

static struct BurnRomInfo SpecdizzyrRomDesc[] = {
	{ "Dizzy - The Ultimate Cartoon Adventure (1987)(Codemasters)(ru)[128K].z80", 0x0c83c, 0x27b9e86c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzyr, Specdizzyr, Spec128)
STD_ROM_FN(Specdizzyr)

struct BurnDriver BurnSpecdizzyr = {
	"spec_dizzyr", "spec_dizzy_48", "spec_spec128", NULL, "1987",
	"Dizzy - The Ultimate Cartoon Adventure (Trainer)(Russian)(128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecdizzyrRomInfo, SpecdizzyrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy - The Ultimate Cartoon Adventure (Russian)(128K)

static struct BurnRomInfo SpecdizzyrstdRomDesc[] = {
	{ "Dizzy - The Ultimate Cartoon Adventure (1987)(Codemasters)(ru)(Standard)(128k).z80", 0x0b6c8, 0x6d8d0a2a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzyrstd, Specdizzyrstd, Spec128)
STD_ROM_FN(Specdizzyrstd)

struct BurnDriver BurnSpecdizzyrstd = {
	"spec_dizzyrstd", "spec_dizzy_48", "spec_spec128", NULL, "1987",
	"Dizzy - The Ultimate Cartoon Adventure (Russian)(128k)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecdizzyrstdRomInfo, SpecdizzyrstdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy - The Ultimate Cartoon Adventure (48K)

static struct BurnRomInfo Specdizzy_48RomDesc[] = {
	{ "Dizzy - The Ultimate Cartoon Adventure (1987)(Codemasters).z80", 0x0b21b, 0xfe79e93a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy_48, Specdizzy_48, Spectrum)
STD_ROM_FN(Specdizzy_48)

struct BurnDriver BurnSpecdizzy_48 = {
	"spec_dizzy_48", NULL, "spec_spectrum", NULL, "1987",
	"Dizzy - The Ultimate Cartoon Adventure (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy_48RomInfo, Specdizzy_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy - The Ultimate Cartoon Adventure (Trainer)(128K)

static struct BurnRomInfo SpecdizzyRomDesc[] = {
	{ "Dizzy - The Ultimate Cartoon Adventure (1987)(Codemasters)[128K].z80", 0x0b82d, 0x30bb57e1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy, Specdizzy, Spec128)
STD_ROM_FN(Specdizzy)

struct BurnDriver BurnSpecdizzy = {
	"spec_dizzy", "spec_dizzy_48", "spec_spec128", NULL, "1987",
	"Dizzy - The Ultimate Cartoon Adventure (Trainer)(128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecdizzyRomInfo, SpecdizzyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy II - Treasure Island Dizzy (Russian) (128K)

static struct BurnRomInfo Specdizzy2rRomDesc[] = {
	{ "Dizzy II - Treasure Island Dizzy (1988)(Codemasters)(ru)[128K].z80", 0x0e0a4, 0xccc7f01b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy2r, Specdizzy2r, Spec128)
STD_ROM_FN(Specdizzy2r)

struct BurnDriver BurnSpecdizzy2r = {
	"spec_dizzy2r", NULL, "spec_spec128", NULL, "1988",
	"Dizzy II - Treasure Island Dizzy (Russian) (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy2rRomInfo, Specdizzy2rRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy II - Treasure Island Dizzy (48K)

static struct BurnRomInfo Specdizzy2_48RomDesc[] = {
	{ "Dizzy II - Treasure Island Dizzy (1988)(Codemasters).z80", 0x0b571, 0x63c7c2a9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy2_48, Specdizzy2_48, Spectrum)
STD_ROM_FN(Specdizzy2_48)

struct BurnDriver BurnSpecdizzy2_48 = {
	"spec_dizzy2_48", "spec_dizzy2", "spec_spectrum", NULL, "1988",
	"Dizzy II - Treasure Island Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy2_48RomInfo, Specdizzy2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy II - Treasure Island Dizzy (128K)

static struct BurnRomInfo Specdizzy2RomDesc[] = {
	{ "Dizzy II - Treasure Island Dizzy (1988)(Codemasters)[128K].z80", 0x0da56, 0x3d2e194d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy2, Specdizzy2, Spec128)
STD_ROM_FN(Specdizzy2)

struct BurnDriver BurnSpecdizzy2 = {
	"spec_dizzy2", NULL, "spec_spec128", NULL, "1988",
	"Dizzy II - Treasure Island Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy2RomInfo, Specdizzy2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy III - Fantasy World Dizzy (Russian) (128K)

static struct BurnRomInfo Specdizzy3rRomDesc[] = {
	{ "Dizzy III - Fantasy World Dizzy (1989)(Codemasters)(ru)[128K].z80", 0x0f2bd, 0x1ae2c460, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy3r, Specdizzy3r, Spec128)
STD_ROM_FN(Specdizzy3r)

struct BurnDriver BurnSpecdizzy3r = {
	"spec_dizzy3r", "spec_dizzy3", "spec_spec128", NULL, "1989",
	"Dizzy III - Fantasy World Dizzy (Russian) (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy3rRomInfo, Specdizzy3rRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy III - Fantasy World Dizzy (48K)

static struct BurnRomInfo Specdizzy3_48RomDesc[] = {
	{ "Dizzy III - Fantasy World Dizzy (1989)(Codemasters).z80", 0x0b391, 0xb4c2f20b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy3_48, Specdizzy3_48, Spectrum)
STD_ROM_FN(Specdizzy3_48)

struct BurnDriver BurnSpecdizzy3_48 = {
	"spec_dizzy3_48", "spec_dizzy3", "spec_spectrum", NULL, "1989",
	"Dizzy III - Fantasy World Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy3_48RomInfo, Specdizzy3_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy III - Fantasy World Dizzy (128K)

static struct BurnRomInfo Specdizzy3RomDesc[] = {
	{ "Dizzy III - Fantasy World Dizzy (1989)(Codemasters)[128K].z80", 0x0f172, 0xef716059, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy3, Specdizzy3, Spec128)
STD_ROM_FN(Specdizzy3)

struct BurnDriver BurnSpecdizzy3 = {
	"spec_dizzy3", NULL, "spec_spec128", NULL, "1989",
	"Dizzy III - Fantasy World Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy3RomInfo, Specdizzy3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy IV - Magicland Dizzy (48K)

static struct BurnRomInfo Specdizzy4_48RomDesc[] = {
	{ "Dizzy IV - Magicland Dizzy (1989)(Codemasters).z80", 0x0b54a, 0x19009d03, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy4_48, Specdizzy4_48, Spectrum)
STD_ROM_FN(Specdizzy4_48)

struct BurnDriver BurnSpecdizzy4_48 = {
	"spec_dizzy4_48", "spec_dizzy4", "spec_spectrum", NULL, "1989",
	"Dizzy IV - Magicland Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy4_48RomInfo, Specdizzy4_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy IV - Magicland Dizzy (128K)

static struct BurnRomInfo Specdizzy4RomDesc[] = {
	{ "Dizzy IV - Magicland Dizzy (1989)(Codemasters)[128K].z80", 0x0e107, 0x94e8903f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy4, Specdizzy4, Spec128)
STD_ROM_FN(Specdizzy4)

struct BurnDriver BurnSpecdizzy4 = {
	"spec_dizzy4", NULL, "spec_spec128", NULL, "1989",
	"Dizzy IV - Magicland Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy4RomInfo, Specdizzy4RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy V - Spellbound Dizzy (48K)

static struct BurnRomInfo Specdizzy5_48RomDesc[] = {
	{ "Dizzy V - Spellbound Dizzy (1991)(Codemasters).z80", 0x0b9c1, 0xdf6849fe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy5_48, Specdizzy5_48, Spectrum)
STD_ROM_FN(Specdizzy5_48)

struct BurnDriver BurnSpecdizzy5_48 = {
	"spec_dizzy5_48", "spec_dizzy5", "spec_spectrum", NULL, "1991",
	"Dizzy V - Spellbound Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy5_48RomInfo, Specdizzy5_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy V - Spellbound Dizzy (128K)

static struct BurnRomInfo Specdizzy5RomDesc[] = {
	{ "Dizzy V - Spellbound Dizzy (1991)(Codemasters)[128K].z80", 0x15e6f, 0x6769bc2e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy5, Specdizzy5, Spec128)
STD_ROM_FN(Specdizzy5)

struct BurnDriver BurnSpecdizzy5 = {
	"spec_dizzy5", NULL, "spec_spec128", NULL, "1991",
	"Dizzy V - Spellbound Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy5RomInfo, Specdizzy5RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy VI - Prince of the Yolkfolk (48K)

static struct BurnRomInfo Specdizzy6_48RomDesc[] = {
	{ "Dizzy VI - Prince of the Yolkfolk (1991)(Codemasters).z80", 0x0a17a, 0xd3791a7a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy6_48, Specdizzy6_48, Spectrum)
STD_ROM_FN(Specdizzy6_48)

struct BurnDriver BurnSpecdizzy6_48 = {
	"spec_dizzy6_48", "spec_dizzy6", "spec_spectrum", NULL, "1991",
	"Dizzy VI - Prince of the Yolkfolk (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy6_48RomInfo, Specdizzy6_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy VI - Prince of the Yolkfolk (128K)

static struct BurnRomInfo Specdizzy6RomDesc[] = {
	{ "Dizzy VI - Prince of the Yolkfolk (1991)(Codemasters)[128K].z80", 0x0b6d2, 0x84574abf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy6, Specdizzy6, Spec128)
STD_ROM_FN(Specdizzy6)

struct BurnDriver BurnSpecdizzy6 = {
	"spec_dizzy6", NULL, "spec_spec128", NULL, "1991",
	"Dizzy VI - Prince of the Yolkfolk (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy6RomInfo, Specdizzy6RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy VII - Crystal Kingdom Dizzy (48K)

static struct BurnRomInfo Specdizzy7_48RomDesc[] = {
	{ "Dizzy VII - Crystal Kingdom Dizzy (1992)(Codemasters).z80", 0x0a58f, 0x9ca9af5e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy7_48, Specdizzy7_48, Spectrum)
STD_ROM_FN(Specdizzy7_48)

struct BurnDriver BurnSpecdizzy7_48 = {
	"spec_dizzy7_48", "spec_dizzy7", "spec_spectrum", NULL, "1992",
	"Dizzy VII - Crystal Kingdom Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy7_48RomInfo, Specdizzy7_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy VII - Crystal Kingdom Dizzy (128K)

static struct BurnRomInfo Specdizzy7RomDesc[] = {
	{ "Dizzy VII - Crystal Kingdom Dizzy (1992)(Codemasters)[128K].z80", 0x0b91d, 0x16fb82f0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy7, Specdizzy7, Spec128)
STD_ROM_FN(Specdizzy7)

struct BurnDriver BurnSpecdizzy7 = {
	"spec_dizzy7", NULL, "spec_spec128", NULL, "1992",
	"Dizzy VII - Crystal Kingdom Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy7RomInfo, Specdizzy7RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy VII - Crystal Kingdom Dizzy (Russian) (128K)

static struct BurnRomInfo Specdizzy7rRomDesc[] = {
	{ "Dizzy VII - Crystal Kingdom Dizzy (1993)(Codemasters)(ru)[128K].z80", 0x0f132, 0xd6b0801d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdizzy7r, Specdizzy7r, Spec128)
STD_ROM_FN(Specdizzy7r)

struct BurnDriver BurnSpecdizzy7r = {
	"spec_dizzy7r", "spec_dizzy7", "spec_spec128", NULL, "1993",
	"Dizzy VII - Crystal Kingdom Dizzy (Russian) (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, Specdizzy7rRomInfo, Specdizzy7rRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Donkey Kong (48K)

static struct BurnRomInfo SpecdkongRomDesc[] = {
	{ "Donkey Kong (1986)(Ocean).z80", 0x09871, 0x4840171d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdkong, Specdkong, Spectrum)
STD_ROM_FN(Specdkong)

struct BurnDriver BurnSpecdkong = {
	"spec_dkong", NULL, "spec_spectrum", NULL, "1986",
	"Donkey Kong (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdkongRomInfo, SpecdkongRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Donkey Kong Jr. (48K)

static struct BurnRomInfo SpecdkongjrRomDesc[] = {
	{ "Donkey Kong Jr. (19xx)(Sir Clive and Mr ZX).z80", 0x02a1d, 0x91569bef, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdkongjr, Specdkongjr, Spectrum)
STD_ROM_FN(Specdkongjr)

struct BurnDriver BurnSpecdkongjr = {
	"spec_dkongjr", NULL, "spec_spectrum", NULL, "19xx",
	"Donkey Kong Jr. (48K)\0", NULL, "Sir Clive and Mr ZX", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdkongjrRomInfo, SpecdkongjrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Doom (demo) (128K)

static struct BurnRomInfo SpecdoomdemoRomDesc[] = {
	{ "Doom (demo) (1996)(Digital Reality)[128K].z80", 0x11d9a, 0xb310b6f1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdoomdemo, Specdoomdemo, Spec128)
STD_ROM_FN(Specdoomdemo)

struct BurnDriver BurnSpecdoomdemo = {
	"spec_doomdemo", NULL, "spec_spec128", NULL, "1996",
	"Doom (demo) (128K)\0", NULL, "Digital Reality", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdoomdemoRomInfo, SpecdoomdemoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Double Dragon III - The Rosetta Stone (128K)

static struct BurnRomInfo Specddragon3RomDesc[] = {
	{ "Double Dragon III - The Rosetta Stone (1991)(Storm Software)[128K].z80", 0x1366a, 0x63257333, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specddragon3, Specddragon3, Spec128)
STD_ROM_FN(Specddragon3)

struct BurnDriver BurnSpecddragon3 = {
	"spec_ddragon3", NULL, "spec_spec128", NULL, "1991",
	"Double Dragon III - The Rosetta Stone (128K)\0", NULL, "Storm Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specddragon3RomInfo, Specddragon3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Draconus (48K)

static struct BurnRomInfo SpecDraconusRomDesc[] = {
	{ "Draconus (1988)(Zeppelin Games).z80", 38737, 0x4364bff8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDraconus, SpecDraconus, Spectrum)
STD_ROM_FN(SpecDraconus)

struct BurnDriver BurnSpecDraconus = {
	"spec_draconus", NULL, "spec_spectrum", NULL, "1988",
	"Draconus (48K)\0", "Press Enter for game menu", "Zeppelin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecDraconusRomInfo, SpecDraconusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dragon Breed (48K)

static struct BurnRomInfo SpecdrgbreedRomDesc[] = {
	{ "Dragon Breed (1989)(Activision).z80", 0x0894c, 0x9cfece69, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdrgbreed, Specdrgbreed, Spectrum)
STD_ROM_FN(Specdrgbreed)

struct BurnDriver BurnSpecdrgbreed = {
	"spec_drgbreed", NULL, "spec_spectrum", NULL, "1989",
	"Dragon Breed (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdrgbreedRomInfo, SpecdrgbreedRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dragon Ninja (Trainer)(128K)

static struct BurnRomInfo SpecdninjaRomDesc[] = {
	{ "Dragon Ninja (1988)(Imagine Software)[128K].z80", 0x186ff, 0xf7171c64, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdninja, Specdninja, Spec128)
STD_ROM_FN(Specdninja)

struct BurnDriver BurnSpecdninja = {
	"spec_dninja", "spec_dninjastd", "spec_spec128", NULL, "1988",
	"Dragon Ninja (Trainer)(128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdninjaRomInfo, SpecdninjaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dragon Ninja (128K)

static struct BurnRomInfo SpecdninjastdRomDesc[] = {
	{ "Dragon Ninja (1988)(Imagine Software)(Standard)(128k).z80", 0x17280, 0xa28f7436, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdninjastd, Specdninjastd, Spec128)
STD_ROM_FN(Specdninjastd)

struct BurnDriver BurnSpecdninjastd = {
	"spec_dninjastd", NULL, "spec_spec128", NULL, "1988",
	"Dragon Ninja (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdninjastdRomInfo, SpecdninjastdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dragontorc (48K)

static struct BurnRomInfo SpecdragontoRomDesc[] = {
	{ "Dragontorc (1985)(Hewson Consultants).z80", 0x09d26, 0xc2e5c32e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdragonto, Specdragonto, Spectrum)
STD_ROM_FN(Specdragonto)

struct BurnDriver BurnSpecdragonto = {
	"spec_dragonto", NULL, "spec_spectrum", NULL, "1985",
	"Dragontorc (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdragontoRomInfo, SpecdragontoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Driller (48K)

static struct BurnRomInfo SpecdrillerRomDesc[] = {
	{ "Driller (1987)(Incentive).z80", 42733, 0x87115b09, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdriller, Specdriller, Spectrum)
STD_ROM_FN(Specdriller)

struct BurnDriver BurnSpecdriller = {
	"spec_driller", NULL, "spec_spectrum", NULL, "1987",
	"Driller (48K)\0", NULL, "Incentive", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecdrillerRomInfo, SpecdrillerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Druid (48K)

static struct BurnRomInfo SpecDruidRomDesc[] = {
	{ "Druid (1986)(Firebird).z80", 39142, 0xcaec88d5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDruid, SpecDruid, Spectrum)
STD_ROM_FN(SpecDruid)

struct BurnDriver BurnSpecDruid = {
	"spec_druid", NULL, "spec_spectrum", NULL, "1986",
	"Druid (48K)\0", "Push 2 to set Kempston Joystick", "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_MAZE, 0,
	SpectrumGetZipName, SpecDruidRomInfo, SpecDruidRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Druid II - Enlightenment (48K)

static struct BurnRomInfo SpecDruid2RomDesc[] = {
	{ "Druid II - Enlightenment (1987)(Firebird).z80", 41484, 0x4822960f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDruid2, SpecDruid2, Spectrum)
STD_ROM_FN(SpecDruid2)

struct BurnDriver BurnSpecDruid2 = {
	"spec_druid2", NULL, "spec_spectrum", NULL, "1987",
	"Druid II - Enlightenment (48K)\0", "Push J to set Kempston Joystick", "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_MAZE, 0,
	SpectrumGetZipName, SpecDruid2RomInfo, SpecDruid2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dynamite Dan (48K)

static struct BurnRomInfo SpecdynadanRomDesc[] = {
	{ "Dynamite Dan (1985)(Mirrorsoft).z80", 0x0a6a8, 0x218460b1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdynadan, Specdynadan, Spectrum)
STD_ROM_FN(Specdynadan)

struct BurnDriver BurnSpecdynadan = {
	"spec_dynadan", NULL, "spec_spectrum", NULL, "1985",
	"Dynamite Dan (48K)\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdynadanRomInfo, SpecdynadanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dynamite Dan II - Dr. Blitzen and the Islands of Arcanum (48K)

static struct BurnRomInfo Specdynadan2RomDesc[] = {
	{ "Dynamite Dan II - Dr Blitzen And The Islands Of Arcanum (1985)(Mirrorsoft).z80", 0x0aaee, 0xdf00027d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdynadan2, Specdynadan2, Spectrum)
STD_ROM_FN(Specdynadan2)

struct BurnDriver BurnSpecdynadan2 = {
	"spec_dynadan2", NULL, "spec_spectrum", NULL, "1986",
	"Dynamite Dan II - Dr. Blitzen and the Islands of Arcanum (48K)\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specdynadan2RomInfo, Specdynadan2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Elite (128K)

static struct BurnRomInfo SpeceliteRomDesc[] = {
	{ "Elite (1985)(Firebird Software)(128k).z80", 0x0a8f4, 0xd914f0df, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specelite, Specelite, Spec128)
STD_ROM_FN(Specelite)

struct BurnDriver BurnSpecelite = {
	"spec_elite", NULL, "spec_spec128", NULL, "1985",
	"Elite (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeceliteRomInfo, SpeceliteRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Elven Warrior (48K)

static struct BurnRomInfo SpecelvewarrRomDesc[] = {
	{ "Elven Warrior (1989)(Players Premier).z80", 0x08e1a, 0x53401159, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specelvewarr, Specelvewarr, Spectrum)
STD_ROM_FN(Specelvewarr)

struct BurnDriver BurnSpecelvewarr = {
	"spec_elvewarr", NULL, "spec_spectrum", NULL, "1989",
	"Elven Warrior (48K)\0", NULL, "Players Premier Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecelvewarrRomInfo, SpecelvewarrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Emilio Butragueno Futbol (Spanish) (128K)

static struct BurnRomInfo SpecemilbutrafutbolRomDesc[] = {
	{ "Emilio Butragueno Futbol (1987)(Topo Soft)(es)(128k).z80", 0x09551, 0xeaef6268, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specemilbutrafutbol, Specemilbutrafutbol, Spec128)
STD_ROM_FN(Specemilbutrafutbol)

struct BurnDriver BurnSpecemilbutrafutbol = {
	"spec_emilbutrafutbol", NULL, "spec_spec128", NULL, "1987",
	"Emilio Butragueno Futbol (Spanish) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecemilbutrafutbolRomInfo, SpecemilbutrafutbolRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Emilio Sanchez Vicario Grand Slam (Spanish) (128K)

static struct BurnRomInfo SpecemilsanchgslamRomDesc[] = {
	{ "Emilio Sanchez Vicario Grand Slam (1989)(Zigurat Software)(es)(128k).z80", 0x0a1ae, 0xab0e96c7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specemilsanchgslam, Specemilsanchgslam, Spec128)
STD_ROM_FN(Specemilsanchgslam)

struct BurnDriver BurnSpecemilsanchgslam = {
	"spec_emilsanchgslam", NULL, "spec_spec128", NULL, "1989",
	"Emilio Sanchez Vicario Grand Slam (Spanish) (128K)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecemilsanchgslamRomInfo, SpecemilsanchgslamRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// E-motion 128K

static struct BurnRomInfo SpecEmotionRomDesc[] = {
	{ "E-motion 128K (1990)(U.S. Gold).z80", 44420, 0xcc29fe26, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEmotion, SpecEmotion, Spec128)
STD_ROM_FN(SpecEmotion)

struct BurnDriver BurnSpecEmotion = {
	"spec_emotion", NULL, "spec_spec128", NULL, "1990",
	"E-motion (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecEmotionRomInfo, SpecEmotionRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Enduro Racer (128K)

static struct BurnRomInfo SpecenduroRomDesc[] = {
	{ "Enduro Racer (1987)(Activision)[128K].z80", 0x0a738, 0xf9d78fc5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specenduro, Specenduro, Spec128)
STD_ROM_FN(Specenduro)

struct BurnDriver BurnSpecenduro = {
	"spec_enduro", NULL, "spec_spec128", NULL, "1987",
	"Enduro Racer (128K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecenduroRomInfo, SpecenduroRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Espada Sagrada, La (Spanish) (128K)

static struct BurnRomInfo SpecespadasagradaRomDesc[] = {
	{ "Espada Sagrada, La (1990)(Topo Soft)(es)(128k).z80", 0x179ba, 0x9c1cd13d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specespadasagrada, Specespadasagrada, Spec128)
STD_ROM_FN(Specespadasagrada)

struct BurnDriver BurnSpecespadasagrada = {
	"spec_espadasagrada", NULL, "spec_spec128", NULL, "1990",
	"Espada Sagrada, La (Spanish) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecespadasagradaRomInfo, SpecespadasagradaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Exolon (48K)

static struct BurnRomInfo Specexolon_48RomDesc[] = {
	{ "Exolon (1987)(Hewson Consultants).z80", 0x0928e, 0xc6b22a79, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specexolon_48, Specexolon_48, Spectrum)
STD_ROM_FN(Specexolon_48)

struct BurnDriver BurnSpecexolon_48 = {
	"spec_exolon_48", "spec_exolon", "spec_spectrum", NULL, "1987",
	"Exolon (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specexolon_48RomInfo, Specexolon_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Exolon (128K)

static struct BurnRomInfo SpecexolonRomDesc[] = {
	{ "Exolon (1987)(Hewson Consultants)(128k).z80", 0x0cd94, 0xab5a464b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specexolon, Specexolon, Spec128)
STD_ROM_FN(Specexolon)

struct BurnDriver BurnSpecexolon = {
	"spec_exolon", NULL, "spec_spec128", NULL, "1987",
	"Exolon (128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecexolonRomInfo, SpecexolonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fantasy Zone 2, The (128K)

static struct BurnRomInfo Specfntzone2RomDesc[] = {
	{ "Fantasy Zone 2, The (19xx)(-)[128K].z80", 0x05c6c, 0xe09d79d8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfntzone2, Specfntzone2, Spec128)
STD_ROM_FN(Specfntzone2)

struct BurnDriver BurnSpecfntzone2 = {
	"spec_fntzone2", NULL, "spec_spec128", NULL, "19xx",
	"Fantasy Zone 2, The (128K)\0", NULL, "Unknown", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specfntzone2RomInfo, Specfntzone2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fast Food Dizzy (48K)

static struct BurnRomInfo Specffdizzy_48RomDesc[] = {
	{ "Fast Food Dizzy (1989)(Codemasters).z80", 0x0a6e6, 0x40b68f49, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specffdizzy_48, Specffdizzy_48, Spectrum)
STD_ROM_FN(Specffdizzy_48)

struct BurnDriver BurnSpecffdizzy_48 = {
	"spec_ffdizzy_48", "spec_ffdizzy", "spec_spectrum", NULL, "1989",
	"Fast Food Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, Specffdizzy_48RomInfo, Specffdizzy_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fast Food Dizzy (128K)

static struct BurnRomInfo SpecffdizzyRomDesc[] = {
	{ "Fast Food Dizzy (1989)(Codemasters)[128K].z80", 0x0bb12, 0x83608e22, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specffdizzy, Specffdizzy, Spec128)
STD_ROM_FN(Specffdizzy)

struct BurnDriver BurnSpecffdizzy = {
	"spec_ffdizzy", NULL, "spec_spec128", NULL, "1989",
	"Fast Food Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecffdizzyRomInfo, SpecffdizzyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fernando Martin Basket Master (Spanish) (128K)

static struct BurnRomInfo SpecfernmartbasketmRomDesc[] = {
	{ "Fernando Martin Basket Master (1987)(Dinamic Software)(es)(128k).z80", 0x0a306, 0x109ea037, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfernmartbasketm, Specfernmartbasketm, Spec128)
STD_ROM_FN(Specfernmartbasketm)

struct BurnDriver BurnSpecfernmartbasketm = {
	"spec_fernmartbasketm", NULL, "spec_spec128", NULL, "1987",
	"Fernando Martin Basket Master (Spanish) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfernmartbasketmRomInfo, SpecfernmartbasketmRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Feud (48K)

static struct BurnRomInfo Specfeud_48RomDesc[] = {
	{ "Feud (1987)(Bulldog).z80", 0x096cf, 0xe9d169a7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfeud_48, Specfeud_48, Spectrum)
STD_ROM_FN(Specfeud_48)

struct BurnDriver BurnSpecfeud_48 = {
	"spec_feud_48", "spec_feud", "spec_spectrum", NULL, "1987",
	"Feud (48K)\0", NULL, "Bulldog", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, Specfeud_48RomInfo, Specfeud_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Feud (128K)

static struct BurnRomInfo SpecfeudRomDesc[] = {
	{ "Feud (1987)(Bulldog)[128K].z80", 0x09d6f, 0x71ccae18, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfeud, Specfeud, Spec128)
STD_ROM_FN(Specfeud)

struct BurnDriver BurnSpecfeud = {
	"spec_feud", NULL, "spec_spec128", NULL, "1987",
	"Feud (128K)\0", NULL, "Bulldog", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, SpecfeudRomInfo, SpecfeudRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Final Matrix, The (128K)

static struct BurnRomInfo SpecfinalmatrixRomDesc[] = {
	{ "Final Matrix, The (1987)(Gremlin Graphics Software)(128k).z80", 0x0bef3, 0xa7f15f73, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfinalmatrix, Specfinalmatrix, Spec128)
STD_ROM_FN(Specfinalmatrix)

struct BurnDriver BurnSpecfinalmatrix = {
	"spec_finalmatrix", NULL, "spec_spec128", NULL, "1987",
	"Final Matrix, The (128K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfinalmatrixRomInfo, SpecfinalmatrixRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Firelord (48K)

static struct BurnRomInfo SpecFirelordRomDesc[] = {
	{ "Firelord (1986)(Hewson Consultants).z80", 42776, 0x0bffeb8b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFirelord, SpecFirelord, Spectrum)
STD_ROM_FN(SpecFirelord)

struct BurnDriver BurnSpecFirelord = {
	"spec_firelord", NULL, "spec_spectrum", NULL, "1986",
	"Firelord (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RPG | GBF_MAZE, 0,
	SpectrumGetZipName, SpecFirelordRomInfo, SpecFirelordRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Flying Shark (128K)

static struct BurnRomInfo SpecfsharkRomDesc[] = {
	{ "Flying Shark (1987)(Firebird Software)(128k).z80", 0x09ac5, 0xc818fccd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfshark, Specfshark, Spec128)
STD_ROM_FN(Specfshark)

struct BurnDriver BurnSpecfshark = {
	"spec_fshark", NULL, "spec_spec128", NULL, "1987",
	"Flying Shark (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfsharkRomInfo, SpecfsharkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Director (48K)

static struct BurnRomInfo SpecfootdireRomDesc[] = {
	{ "Football Director (1986)(D&H Games).z80", 0x09abb, 0x6fdbf2bd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfootdire, Specfootdire, Spectrum)
STD_ROM_FN(Specfootdire)

struct BurnDriver BurnSpecfootdire = {
	"spec_footdire", NULL, "spec_spectrum", NULL, "1986",
	"Football Director (48K)\0", NULL, "D&H Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfootdireRomInfo, SpecfootdireRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager - Players (48K)

static struct BurnRomInfo SpecftmanpRomDesc[] = {
	{ "Football Manager - Players (1982)(Addictive Games).z80", 0x09026, 0x0691e14c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specftmanp, Specftmanp, Spectrum)
STD_ROM_FN(Specftmanp)

struct BurnDriver BurnSpecftmanp = {
	"spec_ftmanp", NULL, "spec_spectrum", NULL, "1982",
	"Football Manager - Players (48K)\0", NULL, "Addictive Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecftmanpRomInfo, SpecftmanpRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager - World Cup Edition (Spanish) (128K)

static struct BurnRomInfo SpecftmanwcRomDesc[] = {
	{ "Football Manager - World Cup Edition (1990)(Addictive Games)(es).z80", 0x0b031, 0x8f0527c8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specftmanwc, Specftmanwc, Spec128)
STD_ROM_FN(Specftmanwc)

struct BurnDriver BurnSpecftmanwc = {
	"spec_ftmanwc", NULL, "spec_spec128", NULL, "1990",
	"Football Manager - World Cup Edition (Spanish) (128K)\0", NULL, "Addictive Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecftmanwcRomInfo, SpecftmanwcRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager (Addictive Games) (48K)

static struct BurnRomInfo SpecftmanRomDesc[] = {
	{ "Football Manager (1982)(Addictive Games).z80", 0x0824d, 0x54fd204c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specftman, Specftman, Spectrum)
STD_ROM_FN(Specftman)

struct BurnDriver BurnSpecftman = {
	"spec_ftman", NULL, "spec_spectrum", NULL, "1982",
	"Football Manager (Addictive Games) (48K)\0", NULL, "Addictive Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecftmanRomInfo, SpecftmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager (Unknown) (48K)

static struct BurnRomInfo SpecftmanaRomDesc[] = {
	{ "Football Manager (19xx)(-).z80", 0x09369, 0xd190faf5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specftmana, Specftmana, Spectrum)
STD_ROM_FN(Specftmana)

struct BurnDriver BurnSpecftmana = {
	"spec_ftmana", "spec_ftman", "spec_spectrum", NULL, "19xx",
	"Football Manager (Unknown) (48K)\0", NULL, "Unknown", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecftmanaRomInfo, SpecftmanaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager 2 - Expansion Kit (48K)

static struct BurnRomInfo Specfootmn2eRomDesc[] = {
	{ "Football Manager 2 - Expansion Kit (1989)(Addictive Games).z80", 0x09efc, 0x155b7053, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfootmn2e, Specfootmn2e, Spectrum)
STD_ROM_FN(Specfootmn2e)

struct BurnDriver BurnSpecfootmn2e = {
	"spec_footmn2e", NULL, "spec_spectrum", NULL, "1989",
	"Football Manager 2 - Expansion Kit (48K)\0", NULL, "Addictive Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specfootmn2eRomInfo, Specfootmn2eRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager 2 (48K)

static struct BurnRomInfo Specfootman2RomDesc[] = {
	{ "Football Manager 2 (1988)(Addictive Games).z80", 0x0aad0, 0xb305dce4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfootman2, Specfootman2, Spectrum)
STD_ROM_FN(Specfootman2)

struct BurnDriver BurnSpecfootman2 = {
	"spec_footman2", NULL, "spec_spectrum", NULL, "1988",
	"Football Manager 2 (48K)\0", NULL, "Addictive Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specfootman2RomInfo, Specfootman2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Football Manager 3 (48K)

static struct BurnRomInfo Specfootman3RomDesc[] = {
	{ "Football Manager 3 (1992)(Addictive Games).z80", 0x069f2, 0x33db96d5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfootman3, Specfootman3, Spectrum)
STD_ROM_FN(Specfootman3)

struct BurnDriver BurnSpecfootman3 = {
	"spec_footman3", NULL, "spec_spectrum", NULL, "1992",
	"Football Manager 3 (48K)\0", NULL, "Addictive Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specfootman3RomInfo, Specfootman3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Footballer of the Year (48K)

static struct BurnRomInfo SpecfotyRomDesc[] = {
	{ "Footballer of the Year (1986)(Gremlin Graphics Software).z80", 0x08137, 0x2af522d0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfoty, Specfoty, Spectrum)
STD_ROM_FN(Specfoty)

struct BurnDriver BurnSpecfoty = {
	"spec_foty", NULL, "spec_spectrum", NULL, "1986",
	"Footballer of the Year (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfotyRomInfo, SpecfotyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Footballer of the Year 2 (48K)

static struct BurnRomInfo Specfoty2RomDesc[] = {
	{ "Footballer of the Year 2 (1987)(Gremlin Graphics Software).z80", 0x08d8f, 0x3722f534, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfoty2, Specfoty2, Spectrum)
STD_ROM_FN(Specfoty2)

struct BurnDriver BurnSpecfoty2 = {
	"spec_foty2", NULL, "spec_spectrum", NULL, "1987",
	"Footballer of the Year 2 (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specfoty2RomInfo, Specfoty2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Forgotten Worlds (128K)

static struct BurnRomInfo SpecforgottnRomDesc[] = {
	{ "Forgotten Worlds (1989)(U.S. Gold)[128K].z80", 0x0e045, 0x33ef767e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specforgottn, Specforgottn, Spec128)
STD_ROM_FN(Specforgottn)

struct BurnDriver BurnSpecforgottn = {
	"spec_forgottn", NULL, "spec_spec128", NULL, "1989",
	"Forgotten Worlds (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecforgottnRomInfo, SpecforgottnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fred (48K)

static struct BurnRomInfo SpecfredRomDesc[] = {
	{ "Fred (1984)(Quicksilva).z80", 0x05d9f, 0xb653c769, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfred, Specfred, Spectrum)
STD_ROM_FN(Specfred)

struct BurnDriver BurnSpecfred = {
	"spec_fred", NULL, "spec_spectrum", NULL, "1984",
	"Fred (48K)\0", NULL, "Quicksilva", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfredRomInfo, SpecfredRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Freddy Hardest (Part 1 of 2) (128k)

static struct BurnRomInfo Specfredhard_128RomDesc[] = {
	{ "Freddy Hardest (1987)(Imagine Software)(Part 1 of 2)(128k).z80", 0x0a968, 0x6638e182, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfredhard_128, Specfredhard_128, Spec128)
STD_ROM_FN(Specfredhard_128)

struct BurnDriver BurnSpecfredhard_128 = {
	"spec_fredhard_128", NULL, "spec_spec128", NULL, "1987",
	"Freddy Hardest (Part 1 of 2) (128k)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specfredhard_128RomInfo, Specfredhard_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Freddy Hardest (Part 2 of 2) (128k)

static struct BurnRomInfo Specfredhard2_128RomDesc[] = {
	{ "Freddy Hardest (1987)(Imagine Software)(Part 2 of 2)(128k).z80", 0x0adbc, 0xf12f7a25, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfredhard2_128, Specfredhard2_128, Spec128)
STD_ROM_FN(Specfredhard2_128)

struct BurnDriver BurnSpecfredhard2_128 = {
	"spec_fredhard2_128", "spec_fredhard_128", "spec_spec128", NULL, "1987",
	"Freddy Hardest (Part 2 of 2) (128k)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specfredhard2_128RomInfo, Specfredhard2_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Frogger (48K)

static struct BurnRomInfo SpecfroggerRomDesc[] = {
	{ "Frogger (1983)(A & F Software).z80", 0x03aa0, 0x52075e8c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfrogger, Specfrogger, Spectrum)
STD_ROM_FN(Specfrogger)

struct BurnDriver BurnSpecfrogger = {
	"spec_frogger", NULL, "spec_spectrum", NULL, "1983",
	"Frogger (48K)\0", NULL, "A & F Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfroggerRomInfo, SpecfroggerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Galaxian (48K)

static struct BurnRomInfo SpecgalaxianRomDesc[] = {
	{ "Galaxian (1984)(Atarisoft).z80", 0x03d8c, 0x8990baef, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgalaxian, Specgalaxian, Spectrum)
STD_ROM_FN(Specgalaxian)

struct BurnDriver BurnSpecgalaxian = {
	"spec_galaxian", NULL, "spec_spectrum", NULL, "1984",
	"Galaxian (48K)\0", NULL, "Atarisoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgalaxianRomInfo, SpecgalaxianRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Game Over (128K)

static struct BurnRomInfo SpecgameoverRomDesc[] = {
	{ "Game Over (1987)(Dinamic Software - Imagine Software)(128k).z80", 0x13750, 0x3dcd6f8e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgameover, Specgameover, Spec128)
STD_ROM_FN(Specgameover)

struct BurnDriver BurnSpecgameover = {
	"spec_gameover", NULL, "spec_spec128", NULL, "1987",
	"Game Over (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgameoverRomInfo, SpecgameoverRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Garfield - Big, Fat, Hairy Deal (48K)

static struct BurnRomInfo SpecGarfieldRomDesc[] = {
	{ "Garfield - Big Fat Hairy Deal (1988)(The Edge).z80", 42525, 0xa2571fc8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGarfield, SpecGarfield, Spectrum)
STD_ROM_FN(SpecGarfield)

struct BurnDriver BurnSpecGarfield = {
	"spec_garfield", NULL, "spec_spectrum", NULL, "1988",
	"Garfield - Big Fat Hairy Deal (48K)\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGarfieldRomInfo, SpecGarfieldRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gary Lineker's Hot-Shot! (48K)

static struct BurnRomInfo SpecglinhtRomDesc[] = {
	{ "Gary Lineker's Hot-Shot! (1988)(Gremlin Graphics Software).z80", 0x08a0a, 0x18e1d943, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specglinht, Specglinht, Spectrum)
STD_ROM_FN(Specglinht)

struct BurnDriver BurnSpecglinht = {
	"spec_glinht", NULL, "spec_spectrum", NULL, "1988",
	"Gary Lineker's Hot-Shot! (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecglinhtRomInfo, SpecglinhtRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gary Lineker's Super Skills (128K)

static struct BurnRomInfo SpecglssRomDesc[] = {
	{ "Gary Lineker's Super Skills (1988)(Gremlin Graphics Software)[128K].z80", 0x12e6f, 0xfcb98fd1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specglss, Specglss, Spec128)
STD_ROM_FN(Specglss)

struct BurnDriver BurnSpecglss = {
	"spec_glss", NULL, "spec_spec128", NULL, "1988",
	"Gary Lineker's Super Skills (128K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecglssRomInfo, SpecglssRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gary Lineker's Super Star Soccer (48K)

static struct BurnRomInfo SpecglsssRomDesc[] = {
	{ "Gary Lineker's Super Star Soccer (1987)(Gremlin Graphics Software).z80", 0x09c44, 0x700248d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specglsss, Specglsss, Spectrum)
STD_ROM_FN(Specglsss)

struct BurnDriver BurnSpecglsss = {
	"spec_glsss", NULL, "spec_spectrum", NULL, "1987",
	"Gary Lineker's Super Star Soccer (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecglsssRomInfo, SpecglsssRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ghosts 'n' Goblins (48K)

static struct BurnRomInfo Specgng_48RomDesc[] = {
	{ "Ghosts 'n' Goblins (1986)(Elite Systems).z80", 0x0b805, 0xdc252529, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgng_48, Specgng_48, Spectrum)
STD_ROM_FN(Specgng_48)

struct BurnDriver BurnSpecgng_48 = {
	"spec_gng_48", "spec_gng", "spec_spectrum", NULL, "1986",
	"Ghosts 'n' Goblins (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgng_48RomInfo, Specgng_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ghostbusters (48K)

static struct BurnRomInfo Specghostb_48RomDesc[] = {
	{ "Ghostbusters (1984)(Activision).z80", 0x07940, 0x8002ad90, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specghostb_48, Specghostb_48, Spectrum)
STD_ROM_FN(Specghostb_48)

struct BurnDriver BurnSpecghostb_48 = {
	"spec_ghostb_48", "spec_ghostb", "spec_spectrum", NULL, "1984",
	"Ghostbusters (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specghostb_48RomInfo, Specghostb_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ghostbusters (128K)

static struct BurnRomInfo SpecghostbRomDesc[] = {
	{ "Ghostbusters (1984)(Activision)[128K].z80", 0x110f7, 0x2b3f6071, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specghostb, Specghostb, Spec128)
STD_ROM_FN(Specghostb)

struct BurnDriver BurnSpecghostb = {
	"spec_ghostb", NULL, "spec_spec128", NULL, "1984",
	"Ghostbusters (128K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecghostbRomInfo, SpecghostbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ghostbusters II (48K)

static struct BurnRomInfo Specghostb2RomDesc[] = {
	{ "Ghostbusters II (1989)(Activision).z80", 0x08f74, 0x018c57e9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specghostb2, Specghostb2, Spectrum)
STD_ROM_FN(Specghostb2)

struct BurnDriver BurnSpecghostb2 = {
	"spec_ghostb2", NULL, "spec_spectrum", NULL, "1989",
	"Ghostbusters II (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specghostb2RomInfo, Specghostb2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ghouls 'n' Ghosts (128K)

static struct BurnRomInfo SpecgngRomDesc[] = {
	{ "Ghouls 'n' Ghosts (1989)(U.S. Gold)[128K].z80", 0x1a8d2, 0x1b626fe8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgng, Specgng, Spec128)
STD_ROM_FN(Specgng)

struct BurnDriver BurnSpecgng = {
	"spec_gng", NULL, "spec_spec128", NULL, "1989",
	"Ghouls 'n' Ghosts (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgngRomInfo, SpecgngRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// G-LOC (128K)

static struct BurnRomInfo SpecglocRomDesc[] = {
	{ "G-LOC (1992)(U.S. Gold)[128K].z80", 0x16f39, 0xeeae7278, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgloc, Specgloc, Spec128)
STD_ROM_FN(Specgloc)

struct BurnDriver BurnSpecgloc = {
	"spec_gloc", NULL, "spec_spec128", NULL, "1992",
	"G-LOC (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecglocRomInfo, SpecglocRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Great Escape, The (48K)

static struct BurnRomInfo SpecgreatescRomDesc[] = {
	{ "Great Escape, The (1986)(Ocean).z80", 0x0aada, 0x7af1372a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgreatesc, Specgreatesc, Spectrum)
STD_ROM_FN(Specgreatesc)

struct BurnDriver BurnSpecgreatesc = {
	"spec_greatesc", NULL, "spec_spectrum", NULL, "1986",
	"Great Escape, The (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgreatescRomInfo, SpecgreatescRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Green Beret (48K)

static struct BurnRomInfo Specgberet_48RomDesc[] = {
	{ "Green Beret (1986)(Imagine Software).z80", 0x0ad53, 0x55f36544, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgberet_48, Specgberet_48, Spectrum)
STD_ROM_FN(Specgberet_48)

struct BurnDriver BurnSpecgberet_48 = {
	"spec_gberet_48", "spec_gberet", "spec_spectrum", NULL, "1986",
	"Green Beret (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgberet_48RomInfo, Specgberet_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Green Beret (128K)

static struct BurnRomInfo SpecgberetRomDesc[] = {
	{ "Green Beret (1986)(Imagine Software)[128K].z80", 0x0b5b3, 0x1b61d4ab, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgberet, Specgberet, Spec128)
STD_ROM_FN(Specgberet)

struct BurnDriver BurnSpecgberet = {
	"spec_gberet", NULL, "spec_spec128", NULL, "1986",
	"Green Beret (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgberetRomInfo, SpecgberetRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gryzor (48K)

static struct BurnRomInfo Specgryzor_48RomDesc[] = {
	{ "Gryzor (1987)(Ocean).z80", 0x08fb8, 0x8024e81b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgryzor_48, Specgryzor_48, Spectrum)
STD_ROM_FN(Specgryzor_48)

struct BurnDriver BurnSpecgryzor_48 = {
	"spec_gryzor_48", "spec_gryzor", "spec_spectrum", NULL, "1987",
	"Gryzor (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgryzor_48RomInfo, Specgryzor_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gryzor (128K)

static struct BurnRomInfo SpecgryzorRomDesc[] = {
	{ "Gryzor (1987)(Ocean)[128K].z80", 0x19b7d, 0x1c8c9d01, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgryzor, Specgryzor, Spec128)
STD_ROM_FN(Specgryzor)

struct BurnDriver BurnSpecgryzor = {
	"spec_gryzor", NULL, "spec_spec128", NULL, "1987",
	"Gryzor (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgryzorRomInfo, SpecgryzorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hammer Boy (Part 1 of 2) (128K)

static struct BurnRomInfo Spechammerboy1RomDesc[] = {
	{ "Hammer Boy (1991)(Dinamic Software)(Part 1 of 2)(128k).z80", 0x0a433, 0x52955ad2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechammerboy1, Spechammerboy1, Spec128)
STD_ROM_FN(Spechammerboy1)

struct BurnDriver BurnSpechammerboy1 = {
	"spec_hammerboy1", NULL, "spec_spec128", NULL, "1991",
	"Hammer Boy (Part 1 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, Spechammerboy1RomInfo, Spechammerboy1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hammer Boy (Part 2 of 2) (128K)

static struct BurnRomInfo Spechammerboy2RomDesc[] = {
	{ "Hammer Boy (1991)(Dinamic Software)(Part 2 of 2)(128k).z80", 0x0ad00, 0x6240dded, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechammerboy2, Spechammerboy2, Spec128)
STD_ROM_FN(Spechammerboy2)

struct BurnDriver BurnSpechammerboy2 = {
	"spec_hammerboy2", "spec_hammerboy1", "spec_spec128", NULL, "1991",
	"Hammer Boy (Part 2 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, Spechammerboy2RomInfo, Spechammerboy2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// H.A.T.E. - Hostile All Terrain Encounter (48K)

static struct BurnRomInfo SpechatehateRomDesc[] = {
	{ "H.A.T.E. - Hostile All Terrain Encounter (1989)(Vortex Software).z80", 0x0a176, 0x21ff36ab, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechatehate, Spechatehate, Spectrum)
STD_ROM_FN(Spechatehate)

struct BurnDriver BurnSpechatehate = {
	"spec_hatehate", NULL, "spec_spectrum", NULL, "1989",
	"H.A.T.E. - Hostile All Terrain Encounter (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechatehateRomInfo, SpechatehateRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hard Drivin' (128K)

static struct BurnRomInfo SpecharddrivRomDesc[] = {
	{ "Hard Drivin' (1989)(Domark)[128K].z80", 0x0adaa, 0xafe65244, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specharddriv, Specharddriv, Spec128)
STD_ROM_FN(Specharddriv)

struct BurnDriver BurnSpecharddriv = {
	"spec_harddriv", NULL, "spec_spec128", NULL, "1989",
	"Hard Drivin' (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecharddrivRomInfo, SpecharddrivRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Head over Heels (48K)

static struct BurnRomInfo Specheadheel_48RomDesc[] = {
	{ "Head over Heels (1987)(Ocean).z80", 0x0a88d, 0x0e74c53b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specheadheel_48, Specheadheel_48, Spectrum)
STD_ROM_FN(Specheadheel_48)

struct BurnDriver BurnSpecheadheel_48 = {
	"spec_headheel_48", "spec_headheel", "spec_spectrum", NULL, "1987",
	"Head over Heels (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specheadheel_48RomInfo, Specheadheel_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Head over Heels (128K)

static struct BurnRomInfo SpecheadheelRomDesc[] = {
	{ "Head over Heels (1987)(Ocean)[128K].z80", 0x0c5a5, 0x9f148037, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specheadheel, Specheadheel, Spec128)
STD_ROM_FN(Specheadheel)

struct BurnDriver BurnSpecheadheel = {
	"spec_headheel", NULL, "spec_spec128", NULL, "1987",
	"Head over Heels (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecheadheelRomInfo, SpecheadheelRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Heart Broken (48K)

static struct BurnRomInfo SpechrtbbrknRomDesc[] = {
	{ "Heart Broken (1989)(Atlantis Software).z80", 0x0a391, 0xed8b78f5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechrtbbrkn, Spechrtbbrkn, Spectrum)
STD_ROM_FN(Spechrtbbrkn)

struct BurnDriver BurnSpechrtbbrkn = {
	"spec_hrtbbrkn", NULL, "spec_spectrum", NULL, "1989",
	"Heart Broken (48K)\0", NULL, "Atlantis Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechrtbbrknRomInfo, SpechrtbbrknRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Heartland (48K)

static struct BurnRomInfo SpecheartlanRomDesc[] = {
	{ "Heartland (1986)(Odin Computer Graphics).z80", 0x0989a, 0x746a07be, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specheartlan, Specheartlan, Spectrum)
STD_ROM_FN(Specheartlan)

struct BurnDriver BurnSpecheartlan = {
	"spec_heartlan", NULL, "spec_spectrum", NULL, "1986",
	"Heartland (48K)\0", NULL, "Odin Computer Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecheartlanRomInfo, SpecheartlanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Highway Encounter (48K)

static struct BurnRomInfo SpechighencoRomDesc[] = {
	{ "Highway Encounter (1985)(Vortex Software).z80", 0x0a36c, 0x7946eec7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechighenco, Spechighenco, Spectrum)
STD_ROM_FN(Spechighenco)

struct BurnDriver BurnSpechighenco = {
	"spec_highenco", NULL, "spec_spectrum", NULL, "1985",
	"Highway Encounter (48K)\0", NULL, "Vortex Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechighencoRomInfo, SpechighencoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hobbit, The v1.0 (48K)

static struct BurnRomInfo Spechobbit2RomDesc[] = {
	{ "Hobbit, The v1.0 (1982)(Melbourne House).z80", 0x0a4c3, 0x46c20d35, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechobbit2, Spechobbit2, Spectrum)
STD_ROM_FN(Spechobbit2)

struct BurnDriver BurnSpechobbit2 = {
	"spec_hobbit2", "spec_hobbit", "spec_spectrum", NULL, "1982",
	"Hobbit, The v1.0 (48K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spechobbit2RomInfo, Spechobbit2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hobbit, The v1.2 (48K)

static struct BurnRomInfo SpechobbitRomDesc[] = {
	{ "Hobbit, The v1.2 (1982)(Melbourne House).z80", 0x0b1ee, 0x10231c84, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechobbit, Spechobbit, Spectrum)
STD_ROM_FN(Spechobbit)

struct BurnDriver BurnSpechobbit = {
	"spec_hobbit", NULL, "spec_spectrum", NULL, "1982",
	"Hobbit, The v1.2 (48K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechobbitRomInfo, SpechobbitRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Horace Goes Skiing (48K)

static struct BurnRomInfo SpechoraceskRomDesc[] = {
	{ "Horace Goes Skiing (1982)(Sinclair Research).z80", 0x0c01e, 0x02cd124b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechoracesk, Spechoracesk, Spectrum)
STD_ROM_FN(Spechoracesk)

struct BurnDriver BurnSpechoracesk = {
	"spec_horacesk", NULL, "spec_spectrum", NULL, "1982",
	"Horace Goes Skiing (48K)\0", NULL, "Sinclair Research", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechoraceskRomInfo, SpechoraceskRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// How to be a Complete Bastard (48K)

static struct BurnRomInfo Spechowbast_48RomDesc[] = {
	{ "How to be a Complete Bastard (1987)(Virgin Games).z80", 0x0a37f, 0x559834ba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechowbast_48, Spechowbast_48, Spectrum)
STD_ROM_FN(Spechowbast_48)

struct BurnDriver BurnSpechowbast_48 = {
	"spec_howbast_48", "spec_howbast", "spec_spectrum", NULL, "1987",
	"How to be a Complete Bastard (48K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spechowbast_48RomInfo, Spechowbast_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// How to be a Complete Bastard (128K)

static struct BurnRomInfo SpechowbastRomDesc[] = {
	{ "How to be a Complete Bastard (1987)(Virgin Games)[128K].z80", 0x0e728, 0x7460da43, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechowbast, Spechowbast, Spec128)
STD_ROM_FN(Spechowbast)

struct BurnDriver BurnSpechowbast = {
	"spec_howbast", NULL, "spec_spec128", NULL, "1987",
	"How to be a Complete Bastard (128K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechowbastRomInfo, SpechowbastRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hudson Hawk (128K)

static struct BurnRomInfo SpechudshawkRomDesc[] = {
	{ "Hudson Hawk (1991)(Ocean)(128k).z80", 0x1a4dc, 0xa55148f8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechudshawk, Spechudshawk, Spec128)
STD_ROM_FN(Spechudshawk)

struct BurnDriver BurnSpechudshawk = {
	"spec_hudshawk", NULL, "spec_spec128", NULL, "1991",
	"Hudson Hawk (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechudshawkRomInfo, SpechudshawkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hunchback (48K)

static struct BurnRomInfo SpechunchbacRomDesc[] = {
	{ "Hunchback (1984)(Ocean).z80", 0x07c76, 0xa7dde347, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechunchbac, Spechunchbac, Spectrum)
STD_ROM_FN(Spechunchbac)

struct BurnDriver BurnSpechunchbac = {
	"spec_hunchbac", NULL, "spec_spectrum", NULL, "1984",
	"Hunchback (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechunchbacRomInfo, SpechunchbacRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hunchback II - Quasimodo's Revenge (48K)

static struct BurnRomInfo Spechnchbac2RomDesc[] = {
	{ "Hunchback II - Quasimodo's Revenge (1985)(Ocean).z80", 0x09254, 0x36eb410c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechnchbac2, Spechnchbac2, Spectrum)
STD_ROM_FN(Spechnchbac2)

struct BurnDriver BurnSpechnchbac2 = {
	"spec_hnchbac2", NULL, "spec_spectrum", NULL, "1985",
	"Hunchback II - Quasimodo's Revenge (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spechnchbac2RomInfo, Spechnchbac2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// I, Ball (128k)

static struct BurnRomInfo Speciball_128RomDesc[] = {
	{ "I, Ball (1987)(Firebird Software)(128k).z80", 0x099ae, 0x54263a76, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speciball_128, Speciball_128, Spec128)
STD_ROM_FN(Speciball_128)

struct BurnDriver BurnSpeciball_128 = {
	"spec_iball_128", NULL, "spec_spec128", NULL, "1987",
	"I, Ball (128k)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speciball_128RomInfo, Speciball_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Incredible Shrinking Fireman, The (48K)

static struct BurnRomInfo SpecincshrfRomDesc[] = {
	{ "Incredible Shrinking Fireman, The (1986)(Mastertronic).z80", 0x08d68, 0x7061d0e3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specincshrf, Specincshrf, Spectrum)
STD_ROM_FN(Specincshrf)

struct BurnDriver BurnSpecincshrf = {
	"spec_incshrf", NULL, "spec_spectrum", NULL, "1986",
	"Incredible Shrinking Fireman, The (48K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecincshrfRomInfo, SpecincshrfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Indoor Soccer (48K)

static struct BurnRomInfo SpecindrsoccrRomDesc[] = {
	{ "Indoor Soccer (1986)(Magnificent 7 Software).z80", 0x070ed, 0x85a1a21f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specindrsoccr, Specindrsoccr, Spectrum)
STD_ROM_FN(Specindrsoccr)

struct BurnDriver BurnSpecindrsoccr = {
	"spec_indrsoccr", NULL, "spec_spectrum", NULL, "1986",
	"Indoor Soccer (48K)\0", NULL, "Magnificent 7 Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecindrsoccrRomInfo, SpecindrsoccrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Into the Eagle's Nest (48K)

static struct BurnRomInfo Specinteagn_48RomDesc[] = {
	{ "Into the Eagle's Nest (1987)(Pandora).z80", 0x07635, 0xcdba827b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specinteagn_48, Specinteagn_48, Spectrum)
STD_ROM_FN(Specinteagn_48)

struct BurnDriver BurnSpecinteagn_48 = {
	"spec_inteagn_48", "spec_inteagn", "spec_spectrum", NULL, "1987",
	"Into the Eagle's Nest (48K)\0", NULL, "Pandora", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specinteagn_48RomInfo, Specinteagn_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Into the Eagle's Nest (128K)

static struct BurnRomInfo SpecinteagnRomDesc[] = {
	{ "Into the Eagle's Nest (1987)(Pandora)(128k).z80", 0x097cf, 0xbc1ea176, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specinteagn, Specinteagn, Spec128)
STD_ROM_FN(Specinteagn)

struct BurnDriver BurnSpecinteagn = {
	"spec_inteagn", NULL, "spec_spec128", NULL, "1987",
	"Into the Eagle's Nest (128K)\0", NULL, "Pandora", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecinteagnRomInfo, SpecinteagnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ivan 'Ironman' Stewart's Super Off Road Racer (48K)

static struct BurnRomInfo Specironman_48RomDesc[] = {
	{ "Ivan 'Ironman' Stewart's Super Off Road Racer (1990)(Virgin Games).z80", 0x08cbf, 0x85841cb9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specironman_48, Specironman_48, Spectrum)
STD_ROM_FN(Specironman_48)

struct BurnDriver BurnSpecironman_48 = {
	"spec_ironman_48", "spec_ironman", "spec_spectrum", NULL, "1990",
	"Ivan 'Ironman' Stewart's Super Off Road Racer (48K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specironman_48RomInfo, Specironman_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ivan 'Ironman' Stewart's Super Off Road Racer (128K)

static struct BurnRomInfo SpecironmanRomDesc[] = {
	{ "Ivan 'Ironman' Stewart's Super Off Road Racer (1990)(Virgin Games)[128K].z80", 0x0932d, 0x1b98e2aa, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specironman, Specironman, Spec128)
STD_ROM_FN(Specironman)

struct BurnDriver BurnSpecironman = {
	"spec_ironman", NULL, "spec_spec128", NULL, "1990",
	"Ivan 'Ironman' Stewart's Super Off Road Racer (128K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecironmanRomInfo, SpecironmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jack the Nipper (128k)

static struct BurnRomInfo Specjacknip_128RomDesc[] = {
	{ "Jack the Nipper (1986)(Gremlin Graphics Software)(128k).z80", 0x0975f, 0xa82eca8c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjacknip_128, Specjacknip_128, Spec128)
STD_ROM_FN(Specjacknip_128)

struct BurnDriver BurnSpecjacknip_128 = {
	"spec_jacknip_128", NULL, "spec_spec128", NULL, "1986",
	"Jack the Nipper (128k)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjacknip_128RomInfo, Specjacknip_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jack the Nipper II - In Coconut Capers (128k)

static struct BurnRomInfo Specjacknip2_128RomDesc[] = {
	{ "Jack the Nipper II - In Coconut Capers (1987)(Gremlin Graphics Software)(128k).z80", 0x0ae26, 0xda98bfe7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjacknip2_128, Specjacknip2_128, Spec128)
STD_ROM_FN(Specjacknip2_128)

struct BurnDriver BurnSpecjacknip2_128 = {
	"spec_jacknip2_128", NULL, "spec_spec128", NULL, "1987",
	"Jack the Nipper II - In Coconut Capers (128k)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjacknip2_128RomInfo, Specjacknip2_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Set Willy (48K)

static struct BurnRomInfo SpecjswillyRomDesc[] = {
	{ "Jet Set Willy (1984)(Software Projects).z80", 0x0667c, 0x1c85a206, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjswilly, Specjswilly, Spectrum)
STD_ROM_FN(Specjswilly)

struct BurnDriver BurnSpecjswilly = {
	"spec_jswilly", NULL, "spec_spectrum", NULL, "1984",
	"Jet Set Willy (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecjswillyRomInfo, SpecjswillyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Set Willy II - The Final Frontier (48K)

static struct BurnRomInfo Specjswilly2RomDesc[] = {
	{ "Jet Set Willy II - The Final Frontier (1985)(Software Projects).z80", 0x0a5d6, 0xfdf35d51, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjswilly2, Specjswilly2, Spectrum)
STD_ROM_FN(Specjswilly2)

struct BurnDriver BurnSpecjswilly2 = {
	"spec_jswilly2", NULL, "spec_spectrum", NULL, "1985",
	"Jet Set Willy II - The Final Frontier (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjswilly2RomInfo, Specjswilly2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Set Willy II - The Final Frontier (end) (48K)

static struct BurnRomInfo Specjswilly2eRomDesc[] = {
	{ "Jet Set Willy II - The Final Frontier (1985)(Software Projects)[end].z80", 0x09f58, 0x27f2f88b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjswilly2e, Specjswilly2e, Spectrum)
STD_ROM_FN(Specjswilly2e)

struct BurnDriver BurnSpecjswilly2e = {
	"spec_jswilly2e", "spec_jswilly2", "spec_spectrum", NULL, "1985",
	"Jet Set Willy II - The Final Frontier (end) (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjswilly2eRomInfo, Specjswilly2eRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Set Willy III (48K)

static struct BurnRomInfo Specjswilly3RomDesc[] = {
	{ "Jet Set Willy III (1985)(MB - APG Software).z80", 0x08061, 0xbde7b5ae, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjswilly3, Specjswilly3, Spectrum)
STD_ROM_FN(Specjswilly3)

struct BurnDriver BurnSpecjswilly3 = {
	"spec_jswilly3", NULL, "spec_spectrum", NULL, "1985",
	"Jet Set Willy III (48K)\0", NULL, "MB - APG Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjswilly3RomInfo, Specjswilly3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jetpac

static struct BurnRomInfo SpecjetpacRomDesc[] = {
	{ "jetpac.z80",     0x02aad, 0x4f96d444, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjetpac, Specjetpac, Spectrum)
STD_ROM_FN(Specjetpac)

struct BurnDriver BurnSpecjetpac = {
	"spec_jetpac", NULL, "spec_spectrum", NULL, "1983",
	"Jetpac (48K)\0", NULL, "Ultimate Play the Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecjetpacRomInfo, SpecjetpacRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jetsons The Computer Game (128K)

static struct BurnRomInfo SpecJetsonsRomDesc[] = {
	{ "Jetsons The Computer Game 128K (1992)(Hi-Tec).z80", 106947, 0x2394a5b6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetsons, SpecJetsons, Spec128)
STD_ROM_FN(SpecJetsons)

struct BurnDriver BurnSpecJetsons = {
	"spec_jetsons", NULL, "spec_spec128", NULL, "1992",
	"Jetsons The Computer Game (128K)\0", "Mega Trainer version", "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecJetsonsRomInfo, SpecJetsonsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Joe Blade (48K)

static struct BurnRomInfo SpecjoebldRomDesc[] = {
	{ "Joe Blade (1987)(Players Premier).z80", 0x097f4, 0xea391957, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjoebld, Specjoebld, Spectrum)
STD_ROM_FN(Specjoebld)

struct BurnDriver BurnSpecjoebld = {
	"spec_joebld", NULL, "spec_spectrum", NULL, "1987",
	"Joe Blade (48K)\0", NULL, "Players Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecjoebldRomInfo, SpecjoebldRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Joe Blade II (48K)

static struct BurnRomInfo Specjoebld2_48RomDesc[] = {
	{ "Joe Blade 2 (1988)(Players Premier).z80", 0x09b61, 0x382b3651, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjoebld2_48, Specjoebld2_48, Spectrum)
STD_ROM_FN(Specjoebld2_48)

struct BurnDriver BurnSpecjoebld2_48 = {
	"spec_joebld2_48", "spec_joebld2", "spec_spectrum", NULL, "1988",
	"Joe Blade II (48K)\0", NULL, "Players Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjoebld2_48RomInfo, Specjoebld2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Joe Blade II (128K)

static struct BurnRomInfo Specjoebld2RomDesc[] = {
	{ "Joe Blade 2 (1988)(Players Premier)(128k).z80", 0x0bb13, 0x20a19599, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjoebld2, Specjoebld2, Spec128)
STD_ROM_FN(Specjoebld2)

struct BurnDriver BurnSpecjoebld2 = {
	"spec_joebld2", NULL, "spec_spec128", NULL, "1988",
	"Joe Blade II (128K)\0", NULL, "Players Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjoebld2RomInfo, Specjoebld2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Joe Blade III (Trainer)(48K)

static struct BurnRomInfo Specjoebld3RomDesc[] = {
	{ "Joe Blade 3 (1989)(Players Premier)[t].z80", 0x0930d, 0x15c34926, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjoebld3, Specjoebld3, Spectrum)
STD_ROM_FN(Specjoebld3)

struct BurnDriver BurnSpecjoebld3 = {
	"spec_joebld3", "spec_joebld3_128", "spec_spectrum", NULL, "1989",
	"Joe Blade III (Trainer)(48K)\0", NULL, "Players Premier Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjoebld3RomInfo, Specjoebld3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Joe Blade III (128K)

static struct BurnRomInfo Specjoebld3_128RomDesc[] = {
	{ "Joe Blade 3 (1989)(Players Premier)(128k).z80", 0x0b400, 0x422359d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjoebld3_128, Specjoebld3_128, Spec128)
STD_ROM_FN(Specjoebld3_128)

struct BurnDriver BurnSpecjoebld3_128 = {
	"spec_joebld3_128", NULL, "spec_spec128", NULL, "1989",
	"Joe Blade III (128K)\0", NULL, "Players Premier", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specjoebld3_128RomInfo, Specjoebld3_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kick Off (48K)

static struct BurnRomInfo SpeckickoffRomDesc[] = {
	{ "Kick Off (1989)(Anco Software).z80", 0x08b95, 0x65b432a1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckickoff, Speckickoff, Spectrum)
STD_ROM_FN(Speckickoff)

struct BurnDriver BurnSpeckickoff = {
	"spec_kickoff", NULL, "spec_spectrum", NULL, "1989",
	"Kick Off (48K)\0", NULL, "Anco Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_SPORTSFOOTBALL, 0,
	SpectrumGetZipName, SpeckickoffRomInfo, SpeckickoffRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kick Off 2

static struct BurnRomInfo SpecKickoff2RomDesc[] = {
	{ "Kick Off 2 (1990)(Anco).tap", 77052, 0x74a921aa, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKickoff2, SpecKickoff2, Spec128)
STD_ROM_FN(SpecKickoff2)

struct BurnDriver BurnSpecKickoff2 = {
	"spec_kickoff2", NULL, "spec_spec128", NULL, "1990",
	"Kick Off 2\0", NULL, "Anco", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_SPORTSFOOTBALL, 0,
	SpectrumGetZipName, SpecKickoff2RomInfo, SpecKickoff2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kick Off World Cup Edition (128K)

static struct BurnRomInfo SpeckickoffwRomDesc[] = {
	{ "Kick Off World Cup Edition (1990)(Anco Software)[128K].z80", 0x09ba6, 0x2a01e70e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckickoffw, Speckickoffw, Spec128)
STD_ROM_FN(Speckickoffw)

struct BurnDriver BurnSpeckickoffw = {
	"spec_kickoffw", NULL, "spec_spec128", NULL, "1990",
	"Kick Off World Cup Edition (128K)\0", NULL, "Anco Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_SPORTSFOOTBALL, 0,
	SpectrumGetZipName, SpeckickoffwRomInfo, SpeckickoffwRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kikstart 2 (48K)

static struct BurnRomInfo Speckikstrt2RomDesc[] = {
	{ "Kikstart 2 (1988)(Mastertronic).z80", 0x08d2d, 0xdb516489, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckikstrt2, Speckikstrt2, Spectrum)
STD_ROM_FN(Speckikstrt2)

struct BurnDriver BurnSpeckikstrt2 = {
	"spec_kikstrt2", NULL, "spec_spectrum", NULL, "1988",
	"Kikstart 2 (48K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speckikstrt2RomInfo, Speckikstrt2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Knight Lore (48K)

static struct BurnRomInfo SpecknigloreRomDesc[] = {
	{ "Knight Lore (1984)(Ultimate Play The Game).z80", 0x08c4c, 0x137ffdb2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckniglore, Speckniglore, Spectrum)
STD_ROM_FN(Speckniglore)

struct BurnDriver BurnSpeckniglore = {
	"spec_kniglore", NULL, "spec_spectrum", NULL, "1984",
	"Knight Lore (48K)\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecknigloreRomInfo, SpecknigloreRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kong (Ocean) (48K)

static struct BurnRomInfo SpeckongRomDesc[] = {
	{ "Kong (1984)(Ocean).z80", 0x0551d, 0x0b3fcf53, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckong, Speckong, Spectrum)
STD_ROM_FN(Speckong)

struct BurnDriver BurnSpeckong = {
	"spec_kong", NULL, "spec_spectrum", NULL, "1984",
	"Kong (Ocean) (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeckongRomInfo, SpeckongRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kong 2 - Kong Strikes Back (48K)

static struct BurnRomInfo Speckong2RomDesc[] = {
	{ "Kong 2 - Kong Strikes Back (1985)(Ocean).z80", 0x06fe3, 0x9f2f534a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckong2, Speckong2, Spectrum)
STD_ROM_FN(Speckong2)

struct BurnDriver BurnSpeckong2 = {
	"spec_kong2", NULL, "spec_spectrum", NULL, "1985",
	"Kong 2 - Kong Strikes Back (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speckong2RomInfo, Speckong2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kung-Fu Master (48K)

static struct BurnRomInfo SpeckungfumRomDesc[] = {
	{ "Kung-Fu Master (1986)(U.S. Gold).z80", 0x0a7a1, 0x7a375d9e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckungfum, Speckungfum, Spectrum)
STD_ROM_FN(Speckungfum)

struct BurnDriver BurnSpeckungfum = {
	"spec_kungfum", NULL, "spec_spectrum", NULL, "1986",
	"Kung-Fu Master (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeckungfumRomInfo, SpeckungfumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kwik Snax Dizzy (48K)

static struct BurnRomInfo Specksdizzy_48RomDesc[] = {
	{ "Kwik Snax Dizzy (1990)(Codemasters).z80", 0x095a5, 0xff50b072, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specksdizzy_48, Specksdizzy_48, Spectrum)
STD_ROM_FN(Specksdizzy_48)

struct BurnDriver BurnSpecksdizzy_48 = {
	"spec_ksdizzy_48", "spec_ksdizzy", "spec_spectrum", NULL, "1990",
	"Kwik Snax Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, Specksdizzy_48RomInfo, Specksdizzy_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kwik Snax Dizzy (128K)

static struct BurnRomInfo SpecksdizzyRomDesc[] = {
	{ "Kwik Snax Dizzy (1990)(Codemasters)[128K].z80", 0x1508b, 0x7e358203, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specksdizzy, Specksdizzy, Spec128)
STD_ROM_FN(Specksdizzy)

struct BurnDriver BurnSpecksdizzy = {
	"spec_ksdizzy", NULL, "spec_spec128", NULL, "1990",
	"Kwik Snax Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecksdizzyRomInfo, SpecksdizzyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Last Ninja 2 (128K)

static struct BurnRomInfo Speclninja2RomDesc[] = {
	{ "Last Ninja 2 (1988)(System 3 Software)[128K].z80", 0x0aac9, 0x35afe78a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclninja2, Speclninja2, Spec128)
STD_ROM_FN(Speclninja2)

struct BurnDriver BurnSpeclninja2 = {
	"spec_lninja2", NULL, "spec_spec128", NULL, "1988",
	"Last Ninja 2 (128K)\0", NULL, "System 3 Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speclninja2RomInfo, Speclninja2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Line of Fire (48K)

static struct BurnRomInfo SpeclinefireRomDesc[] = {
	{ "Line of Fire (1990)(U.S. Gold).z80", 0x09661, 0x9c212b34, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclinefire, Speclinefire, Spectrum)
STD_ROM_FN(Speclinefire)

struct BurnDriver BurnSpeclinefire = {
	"spec_linefire", NULL, "spec_spectrum", NULL, "1990",
	"Line of Fire (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeclinefireRomInfo, SpeclinefireRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Livingstone, I Presume (128K)

static struct BurnRomInfo SpeclivipresRomDesc[] = {
	{ "Livingstone, I Presume (1987)(Opera Soft)(128k).z80", 0x0aff2, 0x50f57c95, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclivipres, Speclivipres, Spec128)
STD_ROM_FN(Speclivipres)

struct BurnDriver BurnSpeclivipres = {
	"spec_livipres", NULL, "spec_spec128", NULL, "1987",
	"Livingstone, I Presume (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeclivipresRomInfo, SpeclivipresRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Livingstone, I Presume II (Part 1 of 2) (128K)

static struct BurnRomInfo Speclivipres21RomDesc[] = {
	{ "Livingstone, I Presume II (1989)(Opera Soft)(Part 1 of 2)(128k).z80", 0x0a7c8, 0xb746f7e6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclivipres21, Speclivipres21, Spec128)
STD_ROM_FN(Speclivipres21)

struct BurnDriver BurnSpeclivipres21 = {
	"spec_livipres21", NULL, "spec_spec128", NULL, "1989",
	"Livingstone, I Presume II (Part 1 of 2) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Speclivipres21RomInfo, Speclivipres21RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Livingstone, I Presume II (Part 2 of 2) (128K)

static struct BurnRomInfo Speclivipres22RomDesc[] = {
	{ "Livingstone, I Presume II (1989)(Opera Soft)(Part 2 of 2)(128k).z80", 0x0943c, 0x85d805e5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclivipres22, Speclivipres22, Spec128)
STD_ROM_FN(Speclivipres22)

struct BurnDriver BurnSpeclivipres22 = {
	"spec_livipres22", "spec_livipres21", "spec_spec128", NULL, "1989",
	"Livingstone, I Presume II (Part 2 of 2) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Speclivipres22RomInfo, Speclivipres22RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lode Runner (Part 1 of 2) (48K)

static struct BurnRomInfo Specloderunn_48RomDesc[] = {
	{ "Lode Runner (1984)(Software Projects)(Part 1 of 2).z80", 0x07db5, 0x60fd844e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specloderunn_48, Specloderunn_48, Spectrum)
STD_ROM_FN(Specloderunn_48)

struct BurnDriver BurnSpecloderunn_48 = {
	"spec_loderunn_48", "spec_loderunn", "spec_spectrum", NULL, "1984",
	"Lode Runner (Part 1 of 2) (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specloderunn_48RomInfo, Specloderunn_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lode Runner (Part 2 of 2) (48K)

static struct BurnRomInfo Specloderunn2_48RomDesc[] = {
	{ "Lode Runner (1984)(Software Projects)(Part 2 of 2).z80", 0x07c15, 0x94d3a6c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specloderunn2_48, Specloderunn2_48, Spectrum)
STD_ROM_FN(Specloderunn2_48)

struct BurnDriver BurnSpecloderunn2_48 = {
	"spec_loderunn2_48", "spec_loderunn", "spec_spectrum", NULL, "1984",
	"Lode Runner (Part 2 of 2) (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specloderunn2_48RomInfo, Specloderunn2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lode Runner (128K)

static struct BurnRomInfo SpecloderunnRomDesc[] = {
	{ "Lode Runner (1984)(Software Projects)[128K].z80", 0x0fab1, 0x937fee1b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specloderunn, Specloderunn, Spec128)
STD_ROM_FN(Specloderunn)

struct BurnDriver BurnSpecloderunn = {
	"spec_loderunn", NULL, "spec_spec128", NULL, "1984",
	"Lode Runner (128K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecloderunnRomInfo, SpecloderunnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lode Runner v2 (48K)

static struct BurnRomInfo Specloderun2RomDesc[] = {
	{ "Lode Runner v2 (1984)(Software Projects).z80", 0x07bf5, 0x4a3ca5b1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specloderun2, Specloderun2, Spectrum)
STD_ROM_FN(Specloderun2)

struct BurnDriver BurnSpecloderun2 = {
	"spec_loderun2", NULL, "spec_spectrum", NULL, "1984",
	"Lode Runner v2 (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specloderun2RomInfo, Specloderun2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lotus Esprit Turbo Challenge (128K)

static struct BurnRomInfo SpeclotustcRomDesc[] = {
	{ "Lotus Esprit Turbo Challenge (1990)(Gremlin Graphics Software)(128k).z80", 0x159b6, 0xe2a3503d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclotustc, Speclotustc, Spec128)
STD_ROM_FN(Speclotustc)

struct BurnDriver BurnSpeclotustc = {
	"spec_lotustc", NULL, "spec_spec128", NULL, "1990",
	"Lotus Esprit Turbo Challenge (128K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpeclotustcRomInfo, SpeclotustcRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lunar Jetman (48K)

static struct BurnRomInfo SpecljetmanRomDesc[] = {
	{ "Lunar Jetman (1983)(Ultimate Play The Game).z80", 0x08e6a, 0x914bc877, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specljetman, Specljetman, Spectrum)
STD_ROM_FN(Specljetman)

struct BurnDriver BurnSpecljetman = {
	"spec_ljetman", NULL, "spec_spectrum", NULL, "1983",
	"Lunar Jetman (48K)\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecljetmanRomInfo, SpecljetmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lunar Rescue (48K)

static struct BurnRomInfo SpeclrescueRomDesc[] = {
	{ "Lunar Rescue (1984)(Lyversoft).z80", 0x046c2, 0x6aac917a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclrescue, Speclrescue, Spectrum)
STD_ROM_FN(Speclrescue)

struct BurnDriver BurnSpeclrescue = {
	"spec_lrescue", NULL, "spec_spectrum", NULL, "1984",
	"Lunar Rescue (48K)\0", NULL, "Lyversoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeclrescueRomInfo, SpeclrescueRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mad Mix Game (Spanish) (128K)

static struct BurnRomInfo SpecmadmixgameRomDesc[] = {
	{ "Mad Mix Game (1988)(Topo Soft)(es)(128k).z80", 0x0a9ad, 0xc35eb329, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmadmixgame, Specmadmixgame, Spec128)
STD_ROM_FN(Specmadmixgame)

struct BurnDriver BurnSpecmadmixgame = {
	"spec_madmixgame", NULL, "spec_spec128", NULL, "1988",
	"Mad Mix Game (Spanish) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmadmixgameRomInfo, SpecmadmixgameRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Manchester United Europe (128K)

static struct BurnRomInfo SpecmanutdeuRomDesc[] = {
	{ "Manchester United Europe (1991)(Krisalis Software)(M5)[128K].z80", 0x0fb26, 0xb4146de0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmanutdeu, Specmanutdeu, Spec128)
STD_ROM_FN(Specmanutdeu)

struct BurnDriver BurnSpecmanutdeu = {
	"spec_manutdeu", NULL, "spec_spec128", NULL, "1991",
	"Manchester United Europe (128K)\0", NULL, "Krisalis Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmanutdeuRomInfo, SpecmanutdeuRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Manic Miner - Eugene - Lord of the Bathroom (48K)

static struct BurnRomInfo SpecmminrelbRomDesc[] = {
	{ "Manic Miner - Eugene - Lord of the Bathroom (1999)(Manic Miner Technologies).z80", 0x07792, 0x3062e7d8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmminrelb, Specmminrelb, Spectrum)
STD_ROM_FN(Specmminrelb)

struct BurnDriver BurnSpecmminrelb = {
	"spec_mminrelb", NULL, "spec_spectrum", NULL, "1999",
	"Manic Miner - Eugene - Lord of the Bathroom (48K)\0", NULL, "Manic Miner Technologies", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmminrelbRomInfo, SpecmminrelbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Manic Miner (Bug-Byte Software) (48K)

static struct BurnRomInfo SpecmminerRomDesc[] = {
	{ "Manic Miner (1983)(Bug-Byte Software).z80", 0x06834, 0x024b1971, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmminer, Specmminer, Spectrum)
STD_ROM_FN(Specmminer)

struct BurnDriver BurnSpecmminer = {
	"spec_mminer", NULL, "spec_spectrum", NULL, "1983",
	"Manic Miner (Bug-Byte Software) (48K)\0", NULL, "Bug-Byte Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmminerRomInfo, SpecmminerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Manic Miner (Software Projects) (48K)

static struct BurnRomInfo SpecmminerspRomDesc[] = {
	{ "Manic Miner (1983)(Software Projects).z80", 0x05ff0, 0x2187b9dd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmminersp, Specmminersp, Spectrum)
STD_ROM_FN(Specmminersp)

struct BurnDriver BurnSpecmminersp = {
	"spec_mminersp", NULL, "spec_spectrum", NULL, "1983",
	"Manic Miner (Software Projects) (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmminerspRomInfo, SpecmminerspRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Marble Madness - Deluxe Edition (48K)

static struct BurnRomInfo SpecmarblemRomDesc[] = {
	{ "Marble Madness - Deluxe Edition (1987)(Melbourne House).z80", 0x0793d, 0x3d08e9ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmarblem, Specmarblem, Spectrum)
STD_ROM_FN(Specmarblem)

struct BurnDriver BurnSpecmarblem = {
	"spec_marblem", NULL, "spec_spectrum", NULL, "1987",
	"Marble Madness - Deluxe Edition (48K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmarblemRomInfo, SpecmarblemRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mario Bros (48K)

static struct BurnRomInfo SpecmaribrosRomDesc[] = {
	{ "Mario Bros (1987)(Ocean).z80", 0x08f38, 0xe42c245c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmaribros, Specmaribros, Spectrum)
STD_ROM_FN(Specmaribros)

struct BurnDriver BurnSpecmaribros = {
	"spec_maribros", NULL, "spec_spectrum", NULL, "1987",
	"Mario Bros (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmaribrosRomInfo, SpecmaribrosRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Match Day (48K)

static struct BurnRomInfo SpecmatchdayRomDesc[] = {
	{ "Match Day (1985)(Ocean).z80", 0x0a809, 0x59d3bc21, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmatchday, Specmatchday, Spectrum)
STD_ROM_FN(Specmatchday)

struct BurnDriver BurnSpecmatchday = {
	"spec_matchday", NULL, "spec_spectrum", NULL, "1985",
	"Match Day (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmatchdayRomInfo, SpecmatchdayRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Match Day II (48K)

static struct BurnRomInfo Specmatchdy2RomDesc[] = {
	{ "Match Day II (1987)(Ocean).z80", 0x09469, 0x910131c2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmatchdy2, Specmatchdy2, Spectrum)
STD_ROM_FN(Specmatchdy2)

struct BurnDriver BurnSpecmatchdy2 = {
	"spec_matchdy2", NULL, "spec_spectrum", NULL, "1987",
	"Match Day II (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmatchdy2RomInfo, Specmatchdy2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Match of the Day (128K)

static struct BurnRomInfo SpecmtchotdRomDesc[] = {
	{ "Match of the Day (1992)(Zeppelin Games)[128K].z80", 0x0cd0a, 0xeb11c05c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmtchotd, Specmtchotd, Spec128)
STD_ROM_FN(Specmtchotd)

struct BurnDriver BurnSpecmtchotd = {
	"spec_mtchotd", NULL, "spec_spec128", NULL, "1992",
	"Match of the Day (128K)\0", NULL, "Zeppelin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmtchotdRomInfo, SpecmtchotdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Merlin (Alternative)(48K)

static struct BurnRomInfo SpecmerlinRomDesc[] = {
	{ "Merlin (1987)(Firebird).z80", 0x096d1, 0x1c2945a1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmerlin, Specmerlin, Spectrum)
STD_ROM_FN(Specmerlin)

struct BurnDriver BurnSpecmerlin = {
	"spec_merlin", "spec_merlin_128", "spec_spectrum", NULL, "1987",
	"Merlin (Alternative)(48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmerlinRomInfo, SpecmerlinRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Merlin (128K)

static struct BurnRomInfo Specmerlin_128RomDesc[] = {
	{ "Merlin (1987)(Firebird)(128k).z80", 0x0a564, 0x34df99db, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmerlin_128, Specmerlin_128, Spec128)
STD_ROM_FN(Specmerlin_128)

struct BurnDriver BurnSpecmerlin_128 = {
	"spec_merlin_128", NULL, "spec_spec128", NULL, "1987",
	"Merlin (128K)\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmerlin_128RomInfo, Specmerlin_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Metal Army (48K)

static struct BurnRomInfo SpecmetaarmyRomDesc[] = {
	{ "Metal Army (1988)(Players Premier).z80", 0x08694, 0xb8b894c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmetaarmy, Specmetaarmy, Spectrum)
STD_ROM_FN(Specmetaarmy)

struct BurnDriver BurnSpecmetaarmy = {
	"spec_metaarmy", NULL, "spec_spectrum", NULL, "1988",
	"Metal Army (48K)\0", NULL, "Players Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmetaarmyRomInfo, SpecmetaarmyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Microprose Soccer (48K)

static struct BurnRomInfo Specmicrsocc_48RomDesc[] = {
	{ "Microprose Soccer (1989)(Microprose Software).z80", 0x07ed8, 0x5125611a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmicrsocc_48, Specmicrsocc_48, Spectrum)
STD_ROM_FN(Specmicrsocc_48)

struct BurnDriver BurnSpecmicrsocc_48 = {
	"spec_micrsocc_48", "spec_micrsocc", "spec_spectrum", NULL, "1989",
	"Microprose Soccer (48K)\0", NULL, "Microprose Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmicrsocc_48RomInfo, Specmicrsocc_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Microprose Soccer (128K)

static struct BurnRomInfo SpecmicrsoccRomDesc[] = {
	{ "Microprose Soccer (1989)(Microprose Software)[128K].z80", 0x09c19, 0x432ea0b4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmicrsocc, Specmicrsocc, Spec128)
STD_ROM_FN(Specmicrsocc)

struct BurnDriver BurnSpecmicrsocc = {
	"spec_micrsocc", NULL, "spec_spec128", NULL, "1989",
	"Microprose Soccer (128K)\0", NULL, "Microprose Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmicrsoccRomInfo, SpecmicrsoccRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Midnight Resistance (48K)

static struct BurnRomInfo Specmresist_48RomDesc[] = {
	{ "Midnight Resistance (1990)(Ocean).z80", 0x0a2e3, 0x70b26b8c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmresist_48, Specmresist_48, Spectrum)
STD_ROM_FN(Specmresist_48)

struct BurnDriver BurnSpecmresist_48 = {
	"spec_mresist_48", "spec_mresist", "spec_spectrum", NULL, "1990",
	"Midnight Resistance (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmresist_48RomInfo, Specmresist_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Midnight Resistance (128K)

static struct BurnRomInfo SpecmresistRomDesc[] = {
	{ "Midnight Resistance (1990)(Ocean)[128K].z80", 0x1b641, 0x614f8f38, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmresist, Specmresist, Spec128)
STD_ROM_FN(Specmresist)

struct BurnDriver BurnSpecmresist = {
	"spec_mresist", NULL, "spec_spec128", NULL, "1990",
	"Midnight Resistance (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmresistRomInfo, SpecmresistRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Monty on the Run (128K)

static struct BurnRomInfo SpecmontrunRomDesc[] = {
	{ "Monty on the Run (1985)(Gremlin Graphics)(128k).z80", 0x09f68, 0x330a393a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmontrun, Specmontrun, Spec128)
STD_ROM_FN(Specmontrun)

struct BurnDriver BurnSpecmontrun = {
	"spec_montrun", NULL, "spec_spec128", NULL, "1985",
	"Monty on the Run (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmontrunRomInfo, SpecmontrunRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moon Cresta - Review (48K)

static struct BurnRomInfo SpecmoocrereRomDesc[] = {
	{ "Moon Cresta - Review (1985)(Incentive Software).z80", 0x0147f, 0x5db616b9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmoocrere, Specmoocrere, Spectrum)
STD_ROM_FN(Specmoocrere)

struct BurnDriver BurnSpecmoocrere = {
	"spec_moocrere", NULL, "spec_spectrum", NULL, "1985",
	"Moon Cresta - Review (48K)\0", NULL, "Incentive Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmoocrereRomInfo, SpecmoocrereRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moon Cresta (48K)

static struct BurnRomInfo SpecmooncrstRomDesc[] = {
	{ "Moon Cresta (1985)(Incentive Software).z80", 0x0979b, 0xaf817ac8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmooncrst, Specmooncrst, Spectrum)
STD_ROM_FN(Specmooncrst)

struct BurnDriver BurnSpecmooncrst = {
	"spec_mooncrst", NULL, "spec_spectrum", NULL, "1985",
	"Moon Cresta (48K)\0", NULL, "Incentive Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmooncrstRomInfo, SpecmooncrstRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moon Torc (48K)

static struct BurnRomInfo SpecmoontorcRomDesc[] = {
	{ "Moon Torc (1991)(Atlantis Software).z80", 0x0a150, 0x8c9406a0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmoontorc, Specmoontorc, Spectrum)
STD_ROM_FN(Specmoontorc)

struct BurnDriver BurnSpecmoontorc = {
	"spec_moontorc", NULL, "spec_spectrum", NULL, "1991",
	"Moon Torc (48K)\0", NULL, "Atlantis Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmoontorcRomInfo, SpecmoontorcRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moonwalker (Part 1 of 3) (48K)

static struct BurnRomInfo SpecmoonwalkRomDesc[] = {
	{ "Moonwalker (1989)(U.S. Gold)[level 1 of 3].z80", 0x09894, 0x20d806f4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmoonwalk, Specmoonwalk, Spectrum)
STD_ROM_FN(Specmoonwalk)

struct BurnDriver BurnSpecmoonwalk = {
	"spec_moonwalk", NULL, "spec_spectrum", NULL, "1989",
	"Moonwalker (Part 1 of 3) (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmoonwalkRomInfo, SpecmoonwalkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moonwalker (Part 2 of 3) (48K)

static struct BurnRomInfo Specmoonwalk2RomDesc[] = {
	{ "Moonwalker (1989)(U.S. Gold)[level 2 of 3].z80", 0x08ac1, 0xd7158141, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmoonwalk2, Specmoonwalk2, Spectrum)
STD_ROM_FN(Specmoonwalk2)

struct BurnDriver BurnSpecmoonwalk2 = {
	"spec_moonwalk2", "spec_moonwalk", "spec_spectrum", NULL, "1989",
	"Moonwalker (Part 2 of 3) (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmoonwalk2RomInfo, Specmoonwalk2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moonwalker (Part 3 of 3) (48K)

static struct BurnRomInfo Specmoonwalk3RomDesc[] = {
	{ "Moonwalker (1989)(U.S. Gold)[level 3 of 3].z80", 0x09cff, 0xa3a6d994, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmoonwalk3, Specmoonwalk3, Spectrum)
STD_ROM_FN(Specmoonwalk3)

struct BurnDriver BurnSpecmoonwalk3 = {
	"spec_moonwalk3", "spec_moonwalk", "spec_spectrum", NULL, "1989",
	"Moonwalker (Part 3 of 3) (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmoonwalk3RomInfo, Specmoonwalk3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mot (Part 1 of 3) (128K)

static struct BurnRomInfo Specmotopera1RomDesc[] = {
	{ "Mot (1989)(Opera Soft)(Part 1 of 3)(128k).z80", 0x09dc2, 0xb7787839, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmotopera1, Specmotopera1, Spec128)
STD_ROM_FN(Specmotopera1)

struct BurnDriver BurnSpecmotopera1 = {
	"spec_motopera1", NULL, "spec_spec128", NULL, "1989",
	"Mot (Part 1 of 3) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmotopera1RomInfo, Specmotopera1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIssue2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mot (Part 2 of 3) (128K)

static struct BurnRomInfo Specmotopera2RomDesc[] = {
	{ "Mot (1989)(Opera Soft)(Part 2 of 3)(128k).z80", 0x09a24, 0xd1c50f2c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmotopera2, Specmotopera2, Spec128)
STD_ROM_FN(Specmotopera2)

struct BurnDriver BurnSpecmotopera2 = {
	"spec_motopera2", "spec_motopera1", "spec_spec128", NULL, "1989",
	"Mot (Part 2 of 3) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmotopera2RomInfo, Specmotopera2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIssue2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mot (Part 3 of 3) (128K)

static struct BurnRomInfo Specmotopera3RomDesc[] = {
	{ "Mot (1989)(Opera Soft)(Part 3 of 3)(128k).z80", 0x09657, 0xdcf09507, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmotopera3, Specmotopera3, Spec128)
STD_ROM_FN(Specmotopera3)

struct BurnDriver BurnSpecmotopera3 = {
	"spec_motopera3", "spec_motopera1", "spec_spec128", NULL, "1989",
	"Mot (Part 3 of 3) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmotopera3RomInfo, Specmotopera3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIssue2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ms. Pac-Man (48K)

static struct BurnRomInfo SpecmspacmanRomDesc[] = {
	{ "Ms. Pac-Man (1984)(Atarisoft).z80", 0x08e51, 0x168677eb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmspacman, Specmspacman, Spectrum)
STD_ROM_FN(Specmspacman)

struct BurnDriver BurnSpecmspacman = {
	"spec_mspacman", NULL, "spec_spectrum", NULL, "1984",
	"Ms. Pac-Man (48K)\0", NULL, "Atarisoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmspacmanRomInfo, SpecmspacmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mundial de Futbol (Spanish) (48K)

static struct BurnRomInfo SpecmundialfutbolRomDesc[] = {
	{ "Mundial de Futbol (1990)(Opera Soft)(es).z80", 0x0910a, 0x493c7bf9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmundialfutbol, Specmundialfutbol, Spectrum)
STD_ROM_FN(Specmundialfutbol)

struct BurnDriver BurnSpecmundialfutbol = {
	"spec_mundialfutbol", NULL, "spec_spectrum", NULL, "1990",
	"Mundial de Futbol (Spanish) (48K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmundialfutbolRomInfo, SpecmundialfutbolRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mutan Zone (Spanish) (Part 1 of 2) (128K)

static struct BurnRomInfo SpecmutazoneRomDesc[] = {
	{ "Mutan Zone (1989)(Opera Soft)(es)(Part 1 of 2)(128k).z80", 0x0ae17, 0xc83a4c4d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmutazone, Specmutazone, Spec128)
STD_ROM_FN(Specmutazone)

struct BurnDriver BurnSpecmutazone = {
	"spec_mutazone", NULL, "spec_spec128", NULL, "1989",
	"Mutan Zone (Spanish) (Part 1 of 2) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmutazoneRomInfo, SpecmutazoneRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mutan Zone (Spanish) (Part 2 of 2) (128K)

static struct BurnRomInfo Specmutazone2RomDesc[] = {
	{ "Mutan Zone (1989)(Opera Soft)(es)(Part 2 of 2)(128k).z80", 0x0adf1, 0x24ee7c22, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmutazone2, Specmutazone2, Spec128)
STD_ROM_FN(Specmutazone2)

struct BurnDriver BurnSpecmutazone2 = {
	"spec_mutazone2", NULL, "spec_spec128", NULL, "1989",
	"Mutan Zone (Spanish) (Part 2 of 2) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmutazone2RomInfo, Specmutazone2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mystery of the Nile, The (128K)

static struct BurnRomInfo SpecmystnileRomDesc[] = {
	{ "Mystery of the Nile, The (1987)(Firebird Software)(128k).z80", 0x0b400, 0xba505791, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmystnile, Specmystnile, Spec128)
STD_ROM_FN(Specmystnile)

struct BurnDriver BurnSpecmystnile = {
	"spec_mystnile", NULL, "spec_spec128", NULL, "1987",
	"Mystery of the Nile, The (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmystnileRomInfo, SpecmystnileRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Navy Moves (Part 1 of 2) (128K)

static struct BurnRomInfo Specnavymoves1RomDesc[] = {
	{ "Navy Moves (1988)(Dinamic Software)(Part 1 of 2)(128k).z80", 0x0aad3, 0x7761958a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnavymoves1, Specnavymoves1, Spec128)
STD_ROM_FN(Specnavymoves1)

struct BurnDriver BurnSpecnavymoves1 = {
	"spec_navymoves1", NULL, "spec_spec128", NULL, "1988",
	"Navy Moves (Part 1 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specnavymoves1RomInfo, Specnavymoves1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Navy Moves (Part 2 of 2) (128K)

static struct BurnRomInfo Specnavymoves2RomDesc[] = {
	{ "Navy Moves (1988)(Dinamic Software)(Part 2 of 2)(128k).z80", 0x0ab4b, 0x0739a125, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnavymoves2, Specnavymoves2, Spec128)
STD_ROM_FN(Specnavymoves2)

struct BurnDriver BurnSpecnavymoves2 = {
	"spec_navymoves2", "spec_navymoves1", "spec_spec128", NULL, "1988",
	"Navy Moves (Part 2 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specnavymoves2RomInfo, Specnavymoves2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Navy SEALs (Part 1 of 2) (128K)

static struct BurnRomInfo SpecnvysealsRomDesc[] = {
	{ "Navy SEALs (1991)(Ocean)(Part 1 of 2)[128K].z80", 0x18df5, 0x40156d33, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnvyseals, Specnvyseals, Spec128)
STD_ROM_FN(Specnvyseals)

struct BurnDriver BurnSpecnvyseals = {
	"spec_nvyseals", NULL, "spec_spec128", NULL, "1991",
	"Navy SEALs (Part 1 of 2) (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnvysealsRomInfo, SpecnvysealsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Navy SEALs (Part 2 of 2) (128K)

static struct BurnRomInfo Specnvyseals2RomDesc[] = {
	{ "Navy SEALs (1991)(Ocean)(Part 2 of 2)[128K].z80", 0x1820a, 0x78ab2c33, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnvyseals2, Specnvyseals2, Spec128)
STD_ROM_FN(Specnvyseals2)

struct BurnDriver BurnSpecnvyseals2 = {
	"spec_nvyseals2", "spec_nvyseals", "spec_spec128", NULL, "1991",
	"Navy SEALs (Part 2 of 2) (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specnvyseals2RomInfo, Specnvyseals2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nebulus (48K)

static struct BurnRomInfo SpecnebulusRomDesc[] = {
	{ "Nebulus (1987)(Hewson Consultants).z80", 0x082f5, 0xa66c873b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnebulus, Specnebulus, Spectrum)
STD_ROM_FN(Specnebulus)

struct BurnDriver BurnSpecnebulus = {
	"spec_nebulus", NULL, "spec_spectrum", NULL, "1987",
	"Nebulus (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnebulusRomInfo, SpecnebulusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nemesis (Konami) (48K)

static struct BurnRomInfo SpecnemesisRomDesc[] = {
	{ "Nemesis (1987)(Konami).z80", 0x09e00, 0xc6c2c9c6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnemesis, Specnemesis, Spectrum)
STD_ROM_FN(Specnemesis)

struct BurnDriver BurnSpecnemesis = {
	"spec_nemesis", NULL, "spec_spectrum", NULL, "1987",
	"Nemesis (Konami) (48K)\0", NULL, "Konami", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnemesisRomInfo, SpecnemesisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nemesis (The Hit Squad) (48K)

static struct BurnRomInfo SpecnemesishsRomDesc[] = {
	{ "Nemesis (1987)(The Hit Squad)[re-release].z80", 0x0a71d, 0xad42684d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnemesishs, Specnemesishs, Spectrum)
STD_ROM_FN(Specnemesishs)

struct BurnDriver BurnSpecnemesishs = {
	"spec_nemesishs", "spec_nemesis", "spec_spectrum", NULL, "1987",
	"Nemesis (The Hit Squad) (48K)\0", NULL, "The Hit Squad", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnemesishsRomInfo, SpecnemesishsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Olli & Lissa - The Ghost of Shilmoore Castle (48K)

static struct BurnRomInfo SpecollilsaRomDesc[] = {
	{ "Olli & Lissa - The Ghost Of Shilmoore Castle (1986)(Firebird).z80", 0x09f58, 0xed38d8ad, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specollilsa, Specollilsa, Spectrum)
STD_ROM_FN(Specollilsa)

struct BurnDriver BurnSpecollilsa = {
	"spec_ollilsa", NULL, "spec_spectrum", NULL, "1986",
	"Olli & Lissa - The Ghost of Shilmoore Castle (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecollilsaRomInfo, SpecollilsaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Olli & Lissa II - Halloween (48K)

static struct BurnRomInfo Specollilsa2RomDesc[] = {
	{ "Olli & Lissa II - Halloween (1987)(Firebird).z80", 0x0a6b1, 0x99e59116, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specollilsa2, Specollilsa2, Spectrum)
STD_ROM_FN(Specollilsa2)

struct BurnDriver BurnSpecollilsa2 = {
	"spec_ollilsa2", NULL, "spec_spectrum", NULL, "1987",
	"Olli & Lissa II - Halloween (48K)\0", NULL, "Silverbird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specollilsa2RomInfo, Specollilsa2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Olli & Lissa III - The Candlelight Adventure (48K)

static struct BurnRomInfo Specollilsa3RomDesc[] = {
	{ "Olli & Lissa III - The Candlelight Adventure (1989)(Codemasters).z80", 0x0a356, 0x7b22d37c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specollilsa3, Specollilsa3, Spectrum)
STD_ROM_FN(Specollilsa3)

struct BurnDriver BurnSpecollilsa3 = {
	"spec_ollilsa3", NULL, "spec_spectrum", NULL, "1989",
	"Olli & Lissa III - The Candlelight Adventure (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specollilsa3RomInfo, Specollilsa3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Operation Thunderbolt (48K)

static struct BurnRomInfo Specothunder_48RomDesc[] = {
	{ "Operation Thunderbolt (1989)(Ocean).z80", 0x09cf8, 0xd1037a4d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specothunder_48, Specothunder_48, Spectrum)
STD_ROM_FN(Specothunder_48)

struct BurnDriver BurnSpecothunder_48 = {
	"spec_othunder_48", "spec_othunder", "spec_spectrum", NULL, "1989",
	"Operation Thunderbolt (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specothunder_48RomInfo, Specothunder_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Operation Thunderbolt (128K)

static struct BurnRomInfo SpecothunderRomDesc[] = {
	{ "Operation Thunderbolt (1989)(Ocean)[128K].z80", 0x1c4e4, 0xcb2c92d8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specothunder, Specothunder, Spec128)
STD_ROM_FN(Specothunder)

struct BurnDriver BurnSpecothunder = {
	"spec_othunder", NULL, "spec_spec128", NULL, "1989",
	"Operation Thunderbolt (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecothunderRomInfo, SpecothunderRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Operation Wolf (48K)

static struct BurnRomInfo Specopwolf_48RomDesc[] = {
	{ "Operation Wolf (1988)(Ocean).z80", 0x0980c, 0x75b79e29, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specopwolf_48, Specopwolf_48, Spectrum)
STD_ROM_FN(Specopwolf_48)

struct BurnDriver BurnSpecopwolf_48 = {
	"spec_opwolf_48", "spec_opwolf", "spec_spectrum", NULL, "1988",
	"Operation Wolf (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specopwolf_48RomInfo, Specopwolf_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Operation Wolf (128K)

static struct BurnRomInfo SpecopwolfRomDesc[] = {
	{ "Operation Wolf (1988)(Ocean)[128K].z80", 0x1da84, 0xa7cd683f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specopwolf, Specopwolf, Spec128)
STD_ROM_FN(Specopwolf)

struct BurnDriver BurnSpecopwolf = {
	"spec_opwolf", NULL, "spec_spec128", NULL, "1988",
	"Operation Wolf (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecopwolfRomInfo, SpecopwolfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pac-Land (48K)

static struct BurnRomInfo Specpacland_48RomDesc[] = {
	{ "Pac-Land (1989)(Grandslam Entertainments).z80", 0x0940e, 0x67075079, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpacland_48, Specpacland_48, Spectrum)
STD_ROM_FN(Specpacland_48)

struct BurnDriver BurnSpecpacland_48 = {
	"spec_pacland_48", "spec_pacland", "spec_spectrum", NULL, "1989",
	"Pac-Land (48K)\0", "Enter to add coins, 1 or 2 to play", "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specpacland_48RomInfo, Specpacland_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pac-Land (128K)

static struct BurnRomInfo SpecpaclandRomDesc[] = {
	{ "Pac-Land (1989)(Grandslam Entertainments)[128K].z80", 0x0b9d7, 0x78fe5d59, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpacland, Specpacland, Spec128)
STD_ROM_FN(Specpacland)

struct BurnDriver BurnSpecpacland = {
	"spec_pacland", NULL, "spec_spec128", NULL, "1989",
	"Pac-Land (128K)\0", "Enter to add coins, 1 or 2 to play", "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecpaclandRomInfo, SpecpaclandRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pac-Man (Atarisoft) (48K)

static struct BurnRomInfo SpecpacmanRomDesc[] = {
	{ "Pac-Man (1983)(Atarisoft).z80", 0x035fc, 0xe3c56f6b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpacman, Specpacman, Spectrum)
STD_ROM_FN(Specpacman)

struct BurnDriver BurnSpecpacman = {
	"spec_pacman", NULL, "spec_spectrum", NULL, "1983",
	"Pac-Man (Atarisoft) (48K)\0", NULL, "Atarisoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpacmanRomInfo, SpecpacmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};
// Pac-Mania (48K)

static struct BurnRomInfo Specpacmania_48RomDesc[] = {
	{ "Pac-Mania (1988)(Grandslam Entertainments).z80", 0x07cb3, 0xb7706dc6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpacmania_48, Specpacmania_48, Spectrum)
STD_ROM_FN(Specpacmania_48)

struct BurnDriver BurnSpecpacmania_48 = {
	"spec_pacmania_48", "spec_pacmania", "spec_spectrum", NULL, "1988",
	"Pac-Mania (48K)\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specpacmania_48RomInfo, Specpacmania_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pac-Mania (128K)

static struct BurnRomInfo SpecpacmaniaRomDesc[] = {
	{ "Pac-Mania (1988)(Grandslam Entertainments)[128K].z80", 0x0a813, 0x938c60ff, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpacmania, Specpacmania, Spec128)
STD_ROM_FN(Specpacmania)

struct BurnDriver BurnSpecpacmania = {
	"spec_pacmania", NULL, "spec_spec128", NULL, "1988",
	"Pac-Mania (128K)\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpacmaniaRomInfo, SpecpacmaniaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pang (128K)

static struct BurnRomInfo SpecpangRomDesc[] = {
	{ "Pang (1990)(Ocean)[128K][incomplete].z80", 0x12130, 0xc7016b68, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpang, Specpang, Spec128)
STD_ROM_FN(Specpang)

struct BurnDriver BurnSpecpang = {
	"spec_pang", NULL, "spec_spec128", NULL, "1990",
	"Pang (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpangRomInfo, SpecpangRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Panic Dizzy (48K)

static struct BurnRomInfo Specpandizzy_48RomDesc[] = {
	{ "Panic Dizzy (1991)(Codemasters).z80", 0x094f5, 0xce5c5125, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpandizzy_48, Specpandizzy_48, Spectrum)
STD_ROM_FN(Specpandizzy_48)

struct BurnDriver BurnSpecpandizzy_48 = {
	"spec_pandizzy_48", "spec_pandizzy", "spec_spectrum", NULL, "1991",
	"Panic Dizzy (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, Specpandizzy_48RomInfo, Specpandizzy_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Panic Dizzy (128K)

static struct BurnRomInfo SpecpandizzyRomDesc[] = {
	{ "Panic Dizzy (1991)(Codemasters)[128K].z80", 0x0b637, 0x4b087391, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpandizzy, Specpandizzy, Spec128)
STD_ROM_FN(Specpandizzy)

struct BurnDriver BurnSpecpandizzy = {
	"spec_pandizzy", NULL, "spec_spec128", NULL, "1991",
	"Panic Dizzy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecpandizzyRomInfo, SpecpandizzyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Paperboy (48K)

static struct BurnRomInfo SpecpaperboyRomDesc[] = {
	{ "Paperboy (1986)(Elite Systems).z80", 0x0a0ba, 0xa1465284, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpaperboy, Specpaperboy, Spectrum)
STD_ROM_FN(Specpaperboy)

struct BurnDriver BurnSpecpaperboy = {
	"spec_paperboy", NULL, "spec_spectrum", NULL, "1986",
	"Paperboy (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpaperboyRomInfo, SpecpaperboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Paperboy 2 (48K)

static struct BurnRomInfo Specpaperby2RomDesc[] = {
	{ "Paperboy 2 (1992)(Mindscape International).z80", 0x09bbe, 0x523b2b3b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpaperby2, Specpaperby2, Spectrum)
STD_ROM_FN(Specpaperby2)

struct BurnDriver BurnSpecpaperby2 = {
	"spec_paperby2", NULL, "spec_spectrum", NULL, "1992",
	"Paperboy 2 (48K)\0", NULL, "Mindscape International", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specpaperby2RomInfo, Specpaperby2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Passing Shot (48K)

static struct BurnRomInfo Specpassshot_48RomDesc[] = {
	{ "Passing Shot (1989)(Image Works).z80", 0x07b0f, 0x183331fc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpassshot_48, Specpassshot_48, Spectrum)
STD_ROM_FN(Specpassshot_48)

struct BurnDriver BurnSpecpassshot_48 = {
	"spec_passshot_48", "spec_passshot", "spec_spectrum", NULL, "1989",
	"Passing Shot (48K)\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specpassshot_48RomInfo, Specpassshot_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Passing Shot (128K)

static struct BurnRomInfo SpecpassshotRomDesc[] = {
	{ "Passing Shot (1989)(Image Works)[128K].z80", 0x098ac, 0xf34e8359, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpassshot, Specpassshot, Spec128)
STD_ROM_FN(Specpassshot)

struct BurnDriver BurnSpecpassshot = {
	"spec_passshot", NULL, "spec_spec128", NULL, "1989",
	"Passing Shot (128K)\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpassshotRomInfo, SpecpassshotRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Peter Shilton's Handball Maradona (48K)

static struct BurnRomInfo SpecpshandmRomDesc[] = {
	{ "Peter Shilton's Handball Maradona (1986)(Grandslam Entertainments).z80", 0x09dff, 0x5d4a8e4d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpshandm, Specpshandm, Spectrum)
STD_ROM_FN(Specpshandm)

struct BurnDriver BurnSpecpshandm = {
	"spec_pshandm", NULL, "spec_spectrum", NULL, "1986",
	"Peter Shilton's Handball Maradona (48K)\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpshandmRomInfo, SpecpshandmRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phantis (Game Over II) (Part 1 of 2) (128K)

static struct BurnRomInfo Specgameover21RomDesc[] = {
	{ "Game Over II (1987)(Dinamic Software)(Part 1 of 2)(128k).z80", 0x09708, 0xf4a22ad9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgameover21, Specgameover21, Spec128)
STD_ROM_FN(Specgameover21)

struct BurnDriver BurnSpecgameover21 = {
	"spec_gameover21", NULL, "spec_spec128", NULL, "1987",
	"Phantis (Game Over II) (Part 1 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgameover21RomInfo, Specgameover21RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phantis (Game Over II) (Part 2 of 2) (128K)

static struct BurnRomInfo Specgameover22RomDesc[] = {
	{ "Game Over II (1987)(Dinamic Software)(Part 2 of 2)(128k).z80", 0x0aa96, 0x4c2a4520, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgameover22, Specgameover22, Spec128)
STD_ROM_FN(Specgameover22)

struct BurnDriver BurnSpecgameover22 = {
	"spec_gameover22", "spec_gameover21", "spec_spec128", NULL, "1987",
	"Phantis (Game Over II) (Part 2 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgameover22RomInfo, Specgameover22RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phoenix (48K)

static struct BurnRomInfo SpecphoenixRomDesc[] = {
	{ "Phoenix (1991)(Zenobi Software).z80", 0x092d3, 0xb2218446, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specphoenix, Specphoenix, Spectrum)
STD_ROM_FN(Specphoenix)

struct BurnDriver BurnSpecphoenix = {
	"spec_phoenix", NULL, "spec_spectrum", NULL, "1991",
	"Phoenix (48K)\0", NULL, "Zenobi Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecphoenixRomInfo, SpecphoenixRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pippo (48K)

static struct BurnRomInfo SpecpippoRomDesc[] = {
	{ "Pippo (1986)(Mastertronic).z80", 0x0b03e, 0xf63e41be, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpippo, Specpippo, Spectrum)
STD_ROM_FN(Specpippo)

struct BurnDriver BurnSpecpippo = {
	"spec_pippo", NULL, "spec_spectrum", NULL, "1986",
	"Pippo (48K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpippoRomInfo, SpecpippoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pixy the Microdot 2 (128k)

static struct BurnRomInfo Specpixymicrod2_128RomDesc[] = {
	{ "Pixy the Microdot 2 (1992)(Your Sinclair)(128k).z80", 0x08df3, 0x1d3c4ccf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpixymicrod2_128, Specpixymicrod2_128, Spec128)
STD_ROM_FN(Specpixymicrod2_128)

struct BurnDriver BurnSpecpixymicrod2_128 = {
	"spec_pixymicrod2_128", NULL, "spec_spec128", NULL, "1992",
	"Pixy the Microdot 2 (128k)\0", NULL, "Your Sinclair", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specpixymicrod2_128RomInfo, Specpixymicrod2_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Platoon (48K)

static struct BurnRomInfo Specplatoon_48RomDesc[] = {
	{ "Platoon (1988)(Ocean).z80", 0x09ae3, 0x06b39aa4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specplatoon_48, Specplatoon_48, Spectrum)
STD_ROM_FN(Specplatoon_48)

struct BurnDriver BurnSpecplatoon_48 = {
	"spec_platoon_48", "spec_platoon", "spec_spectrum", NULL, "1988",
	"Platoon (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specplatoon_48RomInfo, Specplatoon_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Platoon (128K)

static struct BurnRomInfo SpecplatoonRomDesc[] = {
	{ "Platoon (1988)(Ocean)[128K].z80", 0x1a121, 0xaa4d4d13, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specplatoon, Specplatoon, Spec128)
STD_ROM_FN(Specplatoon)

struct BurnDriver BurnSpecplatoon = {
	"spec_platoon", NULL, "spec_spec128", NULL, "1988",
	"Platoon (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecplatoonRomInfo, SpecplatoonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Plotting (48K)

static struct BurnRomInfo Specplotting_48RomDesc[] = {
	{ "Plotting (1990)(Ocean).z80", 0x09950, 0xce3f07c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specplotting_48, Specplotting_48, Spectrum)
STD_ROM_FN(Specplotting_48)

struct BurnDriver BurnSpecplotting_48 = {
	"spec_plotting_48", "spec_plotting", "spec_spectrum", NULL, "1990",
	"Plotting (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specplotting_48RomInfo, Specplotting_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Plotting (128K)

static struct BurnRomInfo SpecplottingRomDesc[] = {
	{ "Plotting (1990)(Ocean)[128K].z80", 0x09fb3, 0xdd65a0b5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specplotting, Specplotting, Spec128)
STD_ROM_FN(Specplotting)

struct BurnDriver BurnSpecplotting = {
	"spec_plotting", NULL, "spec_spec128", NULL, "1990",
	"Plotting (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecplottingRomInfo, SpecplottingRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pole Position (Atarisoft) (48K)

static struct BurnRomInfo SpecpoleposaRomDesc[] = {
	{ "Pole Position (1984)(Atarisoft).z80", 0x0850b, 0x620ff870, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpoleposa, Specpoleposa, Spectrum)
STD_ROM_FN(Specpoleposa)

struct BurnDriver BurnSpecpoleposa = {
	"spec_poleposa", NULL, "spec_spectrum", NULL, "1984",
	"Pole Position (Atarisoft) (48K)\0", NULL, "Atarisoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpoleposaRomInfo, SpecpoleposaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pole Position (U.S. Gold) (48K)

static struct BurnRomInfo SpecpoleposuRomDesc[] = {
	{ "Pole Position (1984)(U.S. Gold).z80", 0x08241, 0x979c15f6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpoleposu, Specpoleposu, Spectrum)
STD_ROM_FN(Specpoleposu)

struct BurnDriver BurnSpecpoleposu = {
	"spec_poleposu", NULL, "spec_spectrum", NULL, "1984",
	"Pole Position (U.S. Gold) (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpoleposuRomInfo, SpecpoleposuRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Poli Diaz (Spanish) (128K)

static struct BurnRomInfo SpecpolidiazRomDesc[] = {
	{ "Poli Diaz (1990)(Opera Soft)(es)(128k).z80", 0x0b232, 0x635c6283, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpolidiaz, Specpolidiaz, Spec128)
STD_ROM_FN(Specpolidiaz)

struct BurnDriver BurnSpecpolidiaz = {
	"spec_polidiaz", NULL, "spec_spec128", NULL, "1990",
	"Poli Diaz (Spanish) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpolidiazRomInfo, SpecpolidiazRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Power Drift (128K)

static struct BurnRomInfo SpecpdriftRomDesc[] = {
	{ "Power Drift (1989)(Activision)[128K].z80", 0x160f6, 0x34a7f74a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpdrift, Specpdrift, Spec128)
STD_ROM_FN(Specpdrift)

struct BurnDriver BurnSpecpdrift = {
	"spec_pdrift", NULL, "spec_spec128", NULL, "1989",
	"Power Drift (128K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpdriftRomInfo, SpecpdriftRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Puzznic (48K)

static struct BurnRomInfo Specpuzznic_48RomDesc[] = {
	{ "Puzznic (1990)(Ocean).z80", 0x06bc9, 0x1b6a8858, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpuzznic_48, Specpuzznic_48, Spectrum)
STD_ROM_FN(Specpuzznic_48)

struct BurnDriver BurnSpecpuzznic_48 = {
	"spec_puzznic_48", "spec_puzznic", "spec_spectrum", NULL, "1990",
	"Puzznic (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specpuzznic_48RomInfo, Specpuzznic_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Puzznic (128K)

static struct BurnRomInfo SpecpuzznicRomDesc[] = {
	{ "Puzznic (1990)(Ocean)[128K].z80", 0x11c44, 0x138fe09d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpuzznic, Specpuzznic, Spec128)
STD_ROM_FN(Specpuzznic)

struct BurnDriver BurnSpecpuzznic = {
	"spec_puzznic", NULL, "spec_spec128", NULL, "1990",
	"Puzznic (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpuzznicRomInfo, SpecpuzznicRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Quartet (48K)

static struct BurnRomInfo SpecquartetRomDesc[] = {
	{ "Quartet (1987)(Activision).z80", 0x08a9e, 0x45711e73, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specquartet, Specquartet, Spectrum)
STD_ROM_FN(Specquartet)

struct BurnDriver BurnSpecquartet = {
	"spec_quartet", NULL, "spec_spectrum", NULL, "1987",
	"Quartet (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecquartetRomInfo, SpecquartetRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Quazatron (48K)

static struct BurnRomInfo SpecquazatroRomDesc[] = {
	{ "Quazatron (1986)(Hewson Consultants).z80", 0x07e39, 0xdf931658, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specquazatro, Specquazatro, Spectrum)
STD_ROM_FN(Specquazatro)

struct BurnDriver BurnSpecquazatro = {
	"spec_quazatro", NULL, "spec_spectrum", NULL, "1986",
	"Quazatron (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecquazatroRomInfo, SpecquazatroRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rainbow Islands - The Story of Bubble Bobble 2 (48K)

static struct BurnRomInfo Specrbisland_48RomDesc[] = {
	{ "Rainbow Islands - The Story of Bubble Bobble 2 (1990)(Ocean).z80", 0x07da0, 0xad2c841f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrbisland_48, Specrbisland_48, Spectrum)
STD_ROM_FN(Specrbisland_48)

struct BurnDriver BurnSpecrbisland_48 = {
	"spec_rbisland_48", "spec_rbisland", "spec_spectrum", NULL, "1990",
	"Rainbow Islands - The Story of Bubble Bobble 2 (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrbisland_48RomInfo, Specrbisland_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rainbow Islands - The Story of Bubble Bobble 2 (128K)

static struct BurnRomInfo SpecrbislandRomDesc[] = {
	{ "Rainbow Islands - The Story of Bubble Bobble 2 (1990)(Ocean)[128K].z80", 0x161bb, 0x0211cd1d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrbisland, Specrbisland, Spec128)
STD_ROM_FN(Specrbisland)

struct BurnDriver BurnSpecrbisland = {
	"spec_rbisland", NULL, "spec_spec128", NULL, "1990",
	"Rainbow Islands - The Story of Bubble Bobble 2 (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrbislandRomInfo, SpecrbislandRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// R.A.M. (Spanish) (128K)

static struct BurnRomInfo SpecramtopoRomDesc[] = {
	{ "RAM (1990)(Topo Soft)(es)(128k).z80", 0x0ae30, 0xc81e6097, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specramtopo, Specramtopo, Spec128)
STD_ROM_FN(Specramtopo)

struct BurnDriver BurnSpecramtopo = {
	"spec_ramtopo", NULL, "spec_spec128", NULL, "1990",
	"R.A.M. (Spanish) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecramtopoRomInfo, SpecramtopoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rambo (48K)

static struct BurnRomInfo SpecramboRomDesc[] = {
	{ "Rambo (1985)(Ocean).z80", 0x09bd0, 0x655b6fa1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrambo, Specrambo, Spectrum)
STD_ROM_FN(Specrambo)

struct BurnDriver BurnSpecrambo = {
	"spec_rambo", NULL, "spec_spectrum", NULL, "1985",
	"Rambo (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecramboRomInfo, SpecramboRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rambo III (128K)

static struct BurnRomInfo Specrambo3RomDesc[] = {
	{ "Rambo III (1988)(Ocean)[128K].z80", 0x1b487, 0xf2b6d24f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrambo3, Specrambo3, Spec128)
STD_ROM_FN(Specrambo3)

struct BurnDriver BurnSpecrambo3 = {
	"spec_rambo3", NULL, "spec_spec128", NULL, "1988",
	"Rambo III (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrambo3RomInfo, Specrambo3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rampage (48K)

static struct BurnRomInfo SpecrampageRomDesc[] = {
	{ "Rampage (1988)(Activision).z80", 0x094eb, 0x3735beaf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrampage, Specrampage, Spectrum)
STD_ROM_FN(Specrampage)

struct BurnDriver BurnSpecrampage = {
	"spec_rampage", NULL, "spec_spectrum", NULL, "1988",
	"Rampage (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrampageRomInfo, SpecrampageRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rastan (128K)

static struct BurnRomInfo SpecrastanRomDesc[] = {
	{ "Rastan (1988)(Imagine Software)[128K].z80", 0x1441f, 0x0440b5e8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrastan, Specrastan, Spec128)
STD_ROM_FN(Specrastan)

struct BurnDriver BurnSpecrastan = {
	"spec_rastan", NULL, "spec_spec128", NULL, "1988",
	"Rastan (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrastanRomInfo, SpecrastanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Renegade (48K)

static struct BurnRomInfo Specrenegade_48RomDesc[] = {
	{ "Renegade (1987)(Imagine Software).z80", 0x0a2d7, 0x9faf0d9e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrenegade_48, Specrenegade_48, Spectrum)
STD_ROM_FN(Specrenegade_48)

struct BurnDriver BurnSpecrenegade_48 = {
	"spec_renegade_48", "spec_renegadestd", "spec_spectrum", NULL, "1987",
	"Renegade (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Specrenegade_48RomInfo, Specrenegade_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Renegade (Trainer)(128K)

static struct BurnRomInfo SpecrenegadeRomDesc[] = {
	{ "Renegade (1987)(Imagine Software)[128K].z80", 0x16f0d, 0xcd930d9a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrenegade, Specrenegade, Spec128)
STD_ROM_FN(Specrenegade)

struct BurnDriver BurnSpecrenegade = {
	"spec_renegade", "spec_renegadestd", "spec_spec128", NULL, "1987",
	"Renegade (Trainer)(128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecrenegadeRomInfo, SpecrenegadeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Renegade (128K)

static struct BurnRomInfo SpecrenegadestdRomDesc[] = {
	{ "Renegade (1987)(Imagine Software)(Standard)(128k).z80", 0x16c76, 0x0ec1661b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrenegadestd, Specrenegadestd, Spec128)
STD_ROM_FN(Specrenegadestd)

struct BurnDriver BurnSpecrenegadestd = {
	"spec_renegadestd", NULL, "spec_spec128", NULL, "1987",
	"Renegade (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecrenegadestdRomInfo, SpecrenegadestdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Renegade II - Target Renegade (128K)

static struct BurnRomInfo Specrenegad2RomDesc[] = {
	{ "Renegade II - Target Renegade (1988)(Imagine Software)[128K].z80", 0x1a950, 0x25d57e2c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrenegad2, Specrenegad2, Spec128)
STD_ROM_FN(Specrenegad2)

struct BurnDriver BurnSpecrenegad2 = {
	"spec_renegad2", NULL, "spec_spec128", NULL, "1988",
	"Renegade II - Target Renegade (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Specrenegad2RomInfo, Specrenegad2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Renegade III - The Final Chapter (128K)

static struct BurnRomInfo Specrenegad3RomDesc[] = {
	{ "Renegade III - The Final Chapter (1989)(Imagine Software)[128K].z80", 0x18519, 0x45f783f9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrenegad3, Specrenegad3, Spec128)
STD_ROM_FN(Specrenegad3)

struct BurnDriver BurnSpecrenegad3 = {
	"spec_renegad3", NULL, "spec_spec128", NULL, "1989",
	"Renegade III - The Final Chapter (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, Specrenegad3RomInfo, Specrenegad3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rex (Part 1 of 2) (48K)

static struct BurnRomInfo Specrex_48RomDesc[] = {
	{ "Rex (1988)(Martech Games)[Part 1 of 2].z80", 0x0aab7, 0xecb7d3dc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrex_48, Specrex_48, Spectrum)
STD_ROM_FN(Specrex_48)

struct BurnDriver BurnSpecrex_48 = {
	"spec_rex_48", "spec_rex", "spec_spectrum", NULL, "1988",
	"Rex (Part 1 of 2) (48K)\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrex_48RomInfo, Specrex_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rex (Part 2 of 2) (48K)

static struct BurnRomInfo Specrex2_48RomDesc[] = {
	{ "Rex (1988)(Martech Games)[Part 2 of 2].z80", 0x09f3a, 0x51130dc0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrex2_48, Specrex2_48, Spectrum)
STD_ROM_FN(Specrex2_48)

struct BurnDriver BurnSpecrex2_48 = {
	"spec_rex2_48", "spec_rex2", "spec_spectrum", NULL, "1988",
	"Rex (Part 2 of 2) (48K)\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrex2_48RomInfo, Specrex2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rex (Part 1 of 2) (128K)

static struct BurnRomInfo SpecrexRomDesc[] = {
	{ "Rex (Part 1)(1988)(Martech Games)(128k).z80", 0x0b137, 0x6b11054c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrex, Specrex, Spec128)
STD_ROM_FN(Specrex)

struct BurnDriver BurnSpecrex = {
	"spec_rex", NULL, "spec_spec128", NULL, "1988",
	"Rex (Part 1 of 2) (128K)\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrexRomInfo, SpecrexRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rex (Part 2 of 2) (128K)

static struct BurnRomInfo Specrex2RomDesc[] = {
	{ "Rex (Part 2)(1988)(Martech Games)(128k).z80", 0x0a5bc, 0x66097e41, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrex2, Specrex2, Spec128)
STD_ROM_FN(Specrex2)

struct BurnDriver BurnSpecrex2 = {
	"spec_rex2", NULL, "spec_spec128", NULL, "1988",
	"Rex (Part 2 of 2) (128K)\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrex2RomInfo, Specrex2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rick Dangerous (48K)

static struct BurnRomInfo SpecrickdangRomDesc[] = {
	{ "Rick Dangerous (1989)(Firebird Software).z80", 0x09cb3, 0x556a8928, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrickdang, Specrickdang, Spectrum)
STD_ROM_FN(Specrickdang)

struct BurnDriver BurnSpecrickdang = {
	"spec_rickdang", NULL, "spec_spectrum", NULL, "1989",
	"Rick Dangerous (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrickdangRomInfo, SpecrickdangRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rick Dangerous 2

static struct BurnRomInfo Specrickdang2RomDesc[] = {
	{ "Rick Dangerous 2 (1990)(Micro Style).tap", 0x26599, 0x5cf79480, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrickdang2, Specrickdang2, Spec128)
STD_ROM_FN(Specrickdang2)

struct BurnDriver BurnSpecrickdang2 = {
	"spec_rickdang2", NULL, "spec_spec128", NULL, "1990",
	"Rick Dangerous 2\0", NULL, "Micro Style", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrickdang2RomInfo, Specrickdang2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Robocop (128K)

static struct BurnRomInfo SpecrobocopRomDesc[] = {
	{ "Robocop (1988)(Ocean)[128K].z80", 0x1cbf8, 0xdcc4bf16, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrobocop, Specrobocop, Spec128)
STD_ROM_FN(Specrobocop)

struct BurnDriver BurnSpecrobocop = {
	"spec_robocop", NULL, "spec_spec128", NULL, "1988",
	"Robocop (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrobocopRomInfo, SpecrobocopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Robocop 2 (128K)

static struct BurnRomInfo Specrobocop2RomDesc[] = {
	{ "Robocop 2 (1990)(Ocean)[128K].z80", 0x1c73e, 0xe9b44bc7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrobocop2, Specrobocop2, Spec128)
STD_ROM_FN(Specrobocop2)

struct BurnDriver BurnSpecrobocop2 = {
	"spec_robocop2", NULL, "spec_spec128", NULL, "1990",
	"Robocop 2 (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrobocop2RomInfo, Specrobocop2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Robocop 3 (128K)

static struct BurnRomInfo Specrobocop3RomDesc[] = {
	{ "Robocop 3 (1992)(Ocean)[128K].z80", 0x1ac8a, 0x21b5c6b7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrobocop3, Specrobocop3, Spec128)
STD_ROM_FN(Specrobocop3)

struct BurnDriver BurnSpecrobocop3 = {
	"spec_robocop3", NULL, "spec_spec128", NULL, "1992",
	"Robocop 3 (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrobocop3RomInfo, Specrobocop3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rock 'n Roller (Spanish) (128K)

static struct BurnRomInfo SpecrocknrollerRomDesc[] = {
	{ "Rock 'n Roller (1988)(Topo Soft)(es)(128k).z80", 0x0b40a, 0xdf57dab8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrocknroller, Specrocknroller, Spec128)
STD_ROM_FN(Specrocknroller)

struct BurnDriver BurnSpecrocknroller = {
	"spec_rocknroller", NULL, "spec_spec128", NULL, "1990",
	"Rock 'n Roller (Spanish) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrocknrollerRomInfo, SpecrocknrollerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rock Star Ate my Hamster (128K)

static struct BurnRomInfo SpecrockshamRomDesc[] = {
	{ "Rock Star Ate my Hamster (1989)(Codemasters)[t][128K].z80", 0x0ffbd, 0xcf617748, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrocksham, Specrocksham, Spec128)
STD_ROM_FN(Specrocksham)

struct BurnDriver BurnSpecrocksham = {
	"spec_rocksham", NULL, "spec_spec128", NULL, "1989",
	"Rock Star Ate my Hamster (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrockshamRomInfo, SpecrockshamRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rolling Thunder (48K)

static struct BurnRomInfo SpecrollthunRomDesc[] = {
	{ "Rolling Thunder (1988)(U.S. Gold).z80", 0x09e57, 0x41bc00e3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrollthun, Specrollthun, Spectrum)
STD_ROM_FN(Specrollthun)

struct BurnDriver BurnSpecrollthun = {
	"spec_rollthun", NULL, "spec_spectrum", NULL, "1988",
	"Rolling Thunder (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrollthunRomInfo, SpecrollthunRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// R-Type (128K)

static struct BurnRomInfo SpecrtypeRomDesc[] = {
	{ "R-Type (1988)(Electric Dreams Software).tap", 0x206d7, 0xa03330a0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrtype, Specrtype, Spec128)
STD_ROM_FN(Specrtype)

struct BurnDriver BurnSpecrtype = {
	"spec_rtype", NULL, "spec_spec128", NULL, "1988",
	"R-Type (128K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrtypeRomInfo, SpecrtypeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ruff and Reddy in the Space Adventure (48K)

static struct BurnRomInfo SpecRuffreddyRomDesc[] = {
	{ "Ruff and Reddy in the Space Adventure 48K (1990)(Hi-Tec).z80", 35879, 0xbb620287, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRuffreddy, SpecRuffreddy, Spectrum)
STD_ROM_FN(SpecRuffreddy)

struct BurnDriver BurnSpecRuffreddy = {
	"spec_ruffreddy", "spec_ruffreddy128k", "spec_spectrum", NULL, "1990",
	"Ruff and Reddy in the Space Adventure (48K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRuffreddyRomInfo, SpecRuffreddyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ruff and Reddy in the Space Adventure (128K)

static struct BurnRomInfo SpecRuffreddy128kRomDesc[] = {
	{ "Ruff and Reddy in the Space Adventure 128K (1990)(Hi-Tec).z80", 43378, 0x61f72f6d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRuffreddy128k, SpecRuffreddy128k, Spec128)
STD_ROM_FN(SpecRuffreddy128k)

struct BurnDriver BurnSpecRuffreddy128k = {
	"spec_ruffreddy128k", NULL, "spec_spec128", NULL, "1990",
	"Ruff and Reddy in the Space Adventure (128K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRuffreddy128kRomInfo, SpecRuffreddy128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Run the Gauntlet (128K)

static struct BurnRomInfo SpecrungauntRomDesc[] = {
	{ "Run the Gauntlet (1989)(Ocean).z80", 0x1a162, 0xe2e3b97c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrungaunt, Specrungaunt, Spec128)
STD_ROM_FN(Specrungaunt)

struct BurnDriver BurnSpecrungaunt = {
	"spec_rungaunt", NULL, "spec_spec128", NULL, "1989",
	"Run the Gauntlet (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrungauntRomInfo, SpecrungauntRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rygar (48K)

static struct BurnRomInfo Specrygar_48RomDesc[] = {
	{ "Rygar (1987)(U.S. Gold).z80", 0x0713e, 0xaaef2c33, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrygar_48, Specrygar_48, Spectrum)
STD_ROM_FN(Specrygar_48)

struct BurnDriver BurnSpecrygar_48 = {
	"spec_rygar_48", "spec_rygar", "spec_spectrum", NULL, "1987",
	"Rygar (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrygar_48RomInfo, Specrygar_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rygar (128K)

static struct BurnRomInfo SpecrygarRomDesc[] = {
	{ "Rygar (1987)(U.S. Gold)[128K].z80", 0x076cc, 0x260afb11, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrygar, Specrygar, Spec128)
STD_ROM_FN(Specrygar)

struct BurnDriver BurnSpecrygar = {
	"spec_rygar", NULL, "spec_spec128", NULL, "1987",
	"Rygar (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrygarRomInfo, SpecrygarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Satan (Part 1 of 2) (128K)

static struct BurnRomInfo SpecsatanRomDesc[] = {
	{ "Satan (1989)(Dinamic Software)(Part 1 of 2)(128k).z80", 0x0898a, 0xd0116da2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsatan, Specsatan, Spec128)
STD_ROM_FN(Specsatan)

struct BurnDriver BurnSpecsatan = {
	"spec_satan", NULL, "spec_spec128", NULL, "1989",
	"Satan (Part 1 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecsatanRomInfo, SpecsatanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Satan (Part 2 of 2) (128K)

static struct BurnRomInfo Specsatan2RomDesc[] = {
	{ "Satan (1989)(Dinamic Software)(Part 2 of 2)(128k).z80", 0x08f4d, 0xd8e20890, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsatan2, Specsatan2, Spec128)
STD_ROM_FN(Specsatan2)

struct BurnDriver BurnSpecsatan2 = {
	"spec_satan2", "spec_satan", "spec_spec128", NULL, "1989",
	"Satan (Part 2 of 2) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specsatan2RomInfo, Specsatan2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.D.I. - Strategic Defence Initiative (48K)

static struct BurnRomInfo SpecsdisdiRomDesc[] = {
	{ "S.D.I. - Strategic Defence Initiative (1988)(Activision).z80", 0x08bad, 0xe0faaf57, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsdisdi, Specsdisdi, Spectrum)
STD_ROM_FN(Specsdisdi)

struct BurnDriver BurnSpecsdisdi = {
	"spec_sdisdi", NULL, "spec_spectrum", NULL, "1988",
	"S.D.I. - Strategic Defence Initiative (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsdisdiRomInfo, SpecsdisdiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Saboteur (Durell Software) (48K)

static struct BurnRomInfo Specsabot_48RomDesc[] = {
	{ "Saboteur (1986)(Durell).z80", 0x095bb, 0xd08cf864, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsabot_48, Specsabot_48, Spectrum)
STD_ROM_FN(Specsabot_48)

struct BurnDriver BurnSpecsabot_48 = {
	"spec_sabot_48", NULL, "spec_spectrum", NULL, "1986",
	"Saboteur (Durell Software) (48K)\0", NULL, "Durell Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsabot_48RomInfo, Specsabot_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Saboteur II - Avenging Angel (Trainer)(48K)

static struct BurnRomInfo Specsabotur2_48RomDesc[] = {
	{ "Saboteur II - Avenging Angel (1987)(Durell).z80", 0x0a996, 0x4f0d8f73, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsabotur2_48, Specsabotur2_48, Spectrum)
STD_ROM_FN(Specsabotur2_48)

struct BurnDriver BurnSpecsabotur2_48 = {
	"spec_sabotur2_48", "spec_sabotur2", "spec_spectrum", NULL, "1987",
	"Saboteur II - Avenging Angel (Trainer)(48K)\0", NULL, "Durell Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsabotur2_48RomInfo, Specsabotur2_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Saboteur II - Avenging Angel (48K)

static struct BurnRomInfo Specsabotur2std_48RomDesc[] = {
	{ "Saboteur II - Avenging Angel (1987)(Durell)(Standard).z80", 0x09ab5, 0x8904b9ba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsabotur2std_48, Specsabotur2std_48, Spectrum)
STD_ROM_FN(Specsabotur2std_48)

struct BurnDriver BurnSpecsabotur2std_48 = {
	"spec_sabotur2std_48", "spec_sabotur2", "spec_spectrum", NULL, "1987",
	"Saboteur II - Avenging Angel (48K)\0", NULL, "Durell Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsabotur2std_48RomInfo, Specsabotur2std_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Saboteur II - Avenging Angel (128K)

static struct BurnRomInfo Specsabotur2RomDesc[] = {
	{ "Saboteur II - Avenging Angel (1987)(Durell)(128k).z80", 0x0b790, 0x7aad77db, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsabotur2, Specsabotur2, Spec128)
STD_ROM_FN(Specsabotur2)

struct BurnDriver BurnSpecsabotur2 = {
	"spec_sabotur2", NULL, "spec_spec128", NULL, "1987",
	"Saboteur II - Avenging Angel (128K)\0", NULL, "Durell Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsabotur2RomInfo, Specsabotur2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Salamander (48K)

static struct BurnRomInfo SpecsalamandRomDesc[] = {
	{ "Salamander (1987)(Imagine Software).z80", 0x0a680, 0x5ae35d91, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsalamand, Specsalamand, Spectrum)
STD_ROM_FN(Specsalamand)

struct BurnDriver BurnSpecsalamand = {
	"spec_salamand", NULL, "spec_spectrum", NULL, "1987",
	"Salamander (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsalamandRomInfo, SpecsalamandRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Savage (Part 1 of 3) (48K)

static struct BurnRomInfo Specsavage1RomDesc[] = {
	{ "Savage, The (1988)(Firebird)[Part 1 of 3].z80", 0x0921c, 0xf8071892, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsavage1, Specsavage1, Spectrum)
STD_ROM_FN(Specsavage1)

struct BurnDriver BurnSpecsavage1 = {
	"spec_savage1", NULL, "spec_spectrum", NULL, "1988",
	"Savage (Part 1 of 3) (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsavage1RomInfo, Specsavage1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Savage (Part 2 of 3) (48K)

static struct BurnRomInfo Specsavage2RomDesc[] = {
	{ "Savage, The (1988)(Firebird)[Part 2 of 3].z80", 0x09a20, 0x4f8ddec1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsavage2, Specsavage2, Spectrum)
STD_ROM_FN(Specsavage2)

struct BurnDriver BurnSpecsavage2 = {
	"spec_savage2", "spec_savage1", "spec_spectrum", NULL, "1988",
	"Savage (Part 2 of 3) (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, Specsavage2RomInfo, Specsavage2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Savage (Part 3 of 3) (48K)

static struct BurnRomInfo Specsavage3RomDesc[] = {
	{ "Savage, The (1988)(Firebird)[Part 3 of 3].z80", 0x08475, 0xe994f627, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsavage3, Specsavage3, Spectrum)
STD_ROM_FN(Specsavage3)

struct BurnDriver BurnSpecsavage3 = {
	"spec_savage3", "spec_savage1", "spec_spectrum", NULL, "1988",
	"Savage (Part 3 of 3) (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsavage3RomInfo, Specsavage3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Scramble Spirits (48K)

static struct BurnRomInfo Specscrspirt_48RomDesc[] = {
	{ "Scramble Spirits (1990)(Grandslam Entertainments).z80", 0x092c7, 0xf292ffc7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specscrspirt_48, Specscrspirt_48, Spectrum)
STD_ROM_FN(Specscrspirt_48)

struct BurnDriver BurnSpecscrspirt_48 = {
	"spec_scrspirt_48", "spec_scrspirt", "spec_spectrum", NULL, "1990",
	"Scramble Spirits (48K)\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, Specscrspirt_48RomInfo, Specscrspirt_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Scramble Spirits (128K)

static struct BurnRomInfo SpecscrspirtRomDesc[] = {
	{ "Scramble Spirits (1990)(Grandslam Entertainments)[128K].z80", 0x14309, 0xf293694e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specscrspirt, Specscrspirt, Spec128)
STD_ROM_FN(Specscrspirt)

struct BurnDriver BurnSpecscrspirt = {
	"spec_scrspirt", NULL, "spec_spec128", NULL, "1990",
	"Scramble Spirits (128K)\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecscrspirtRomInfo, SpecscrspirtRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Seymour At The Movies (48K)

static struct BurnRomInfo Specseymmovi_48RomDesc[] = {
	{ "Seymour At The Movies (1991)(Codemasters)(48K).z80", 0x0b3c7, 0xc91a5fa5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specseymmovi_48, Specseymmovi_48, Spectrum)
STD_ROM_FN(Specseymmovi_48)

struct BurnDriver BurnSpecseymmovi = {
	"spec_seymmovi_48", "spec_seymmovi", "spec_spectrum", NULL, "1991",
	"Seymour at the Movies (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specseymmovi_48RomInfo, Specseymmovi_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Seymour At The Movies (128K)

static struct BurnRomInfo SpecSeymmoviRomDesc[] = {
	{ "Seymour At the Movies (1991)(Codemasters)(128K).z80", 79527, 0xd1a72ae0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSeymmovi, SpecSeymmovi, Spec128)
STD_ROM_FN(SpecSeymmovi)

struct BurnDriver BurnSpecSeymmovi = {
	"spec_seymmovi", NULL, "spec_spec128", NULL, "1991",
	"Seymour At The Movies (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSeymmoviRomInfo, SpecSeymmoviRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Seymour Take One (48K)

static struct BurnRomInfo SpecseytakonRomDesc[] = {
	{ "Seymour Take One (1991)(Codemasters).z80", 0x0858a, 0x21645f38, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specseytakon, Specseytakon, Spectrum)
STD_ROM_FN(Specseytakon)

struct BurnDriver BurnSpecseytakon = {
	"spec_seytakon", NULL, "spec_spectrum", NULL, "1991",
	"Seymour Take One (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecseytakonRomInfo, SpecseytakonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shadow Warriors (128K)

static struct BurnRomInfo SpecshadwarrRomDesc[] = {
	{ "Shadow Warriors (1990)(Ocean)[128K].z80", 0x1c463, 0x8a034e94, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshadwarr, Specshadwarr, Spec128)
STD_ROM_FN(Specshadwarr)

struct BurnDriver BurnSpecshadwarr = {
	"spec_shadwarr", NULL, "spec_spec128", NULL, "1990",
	"Shadow Warriors (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecshadwarrRomInfo, SpecshadwarrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shinobi (128K)

static struct BurnRomInfo SpecshinobiRomDesc[] = {
	{ "Shinobi (1989)(Virgin Games)[128K].z80", 0x0d8b0, 0x3ca7a9e7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshinobi, Specshinobi, Spec128)
STD_ROM_FN(Specshinobi)

struct BurnDriver BurnSpecshinobi = {
	"spec_shinobi", NULL, "spec_spec128", NULL, "1989",
	"Shinobi (128K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecshinobiRomInfo, SpecshinobiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Silkworm (128K)

static struct BurnRomInfo SpecsilkwormRomDesc[] = {
	{ "Silkworm (1989)(Virgin Games)[128K].z80", 0x174d9, 0xeb973e1c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsilkworm, Specsilkworm, Spec128)
STD_ROM_FN(Specsilkworm)

struct BurnDriver BurnSpecsilkworm = {
	"spec_silkworm", NULL, "spec_spec128", NULL, "1989",
	"Silkworm (128K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecsilkwormRomInfo, SpecsilkwormRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sim City (48K)

static struct BurnRomInfo SpecsimcityRomDesc[] = {
	{ "Sim City (1989)(Infogrames).z80", 0x066f0, 0x83ec2144, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsimcity, Specsimcity, Spectrum)
STD_ROM_FN(Specsimcity)

struct BurnDriver BurnSpecsimcity = {
	"spec_simcity", NULL, "spec_spectrum", NULL, "1989",
	"Sim City (48K)\0", NULL, "Infogrames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsimcityRomInfo, SpecsimcityRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Simulador Profesional de Tenis (Spanish) (128K)

static struct BurnRomInfo SpecsimulprotenisRomDesc[] = {
	{ "Simulador Profesional de Tenis (1990)(Dinamic Software)(es)(128k).z80", 0x0ae1d, 0xdc939284, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsimulprotenis, Specsimulprotenis, Spec128)
STD_ROM_FN(Specsimulprotenis)

struct BurnDriver BurnSpecsimulprotenis = {
	"spec_simulprotenis", NULL, "spec_spec128", NULL, "1990",
	"Simulador Profesional de Tenis (Spanish) (128K)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsimulprotenisRomInfo, SpecsimulprotenisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sir Fred (128k)

static struct BurnRomInfo Specsirfred_128RomDesc[] = {
	{ "Sir Fred (1986)(Mikro-Gen)(128k).z80", 0x0a8b5, 0xb57f2c22, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsirfred_128, Specsirfred_128, Spec128)
STD_ROM_FN(Specsirfred_128)

struct BurnDriver BurnSpecsirfred_128 = {
	"spec_sirfred_128", NULL, "spec_spec128", NULL, "1986",
	"Sir Fred (128k)\0", NULL, "Mikro-Gen", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsirfred_128RomInfo, Specsirfred_128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sirwood (Part 1 of 3) (128K)

static struct BurnRomInfo Specsirwoodg1RomDesc[] = {
	{ "Sirwood (1990)(Opera Soft)(Part 1 of 3)(128k).z80", 0x09992, 0xafd2f222, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsirwoodg1, Specsirwoodg1, Spec128)
STD_ROM_FN(Specsirwoodg1)

struct BurnDriver BurnSpecsirwoodg1 = {
	"spec_sirwoodg1", NULL, "spec_spec128", NULL, "1990",
	"Sirwood (Part 1 of 3) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsirwoodg1RomInfo, Specsirwoodg1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sirwood (Part 2 of 3) (128K)

static struct BurnRomInfo Specsirwoodg2RomDesc[] = {
	{ "Sirwood (1990)(Opera Soft)(Part 2 of 3)(128k).z80", 0x098b4, 0x21ead039, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsirwoodg2, Specsirwoodg2, Spec128)
STD_ROM_FN(Specsirwoodg2)

struct BurnDriver BurnSpecsirwoodg2 = {
	"spec_sirwoodg2", "spec_sirwoodg1", "spec_spec128", NULL, "1990",
	"Sirwood (Part 2 of 3) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsirwoodg2RomInfo, Specsirwoodg2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sirwood (Part 3 of 3) (128K)

static struct BurnRomInfo Specsirwoodg3RomDesc[] = {
	{ "Sirwood (1990)(Opera Soft)(Part 3 of 3)(128k).z80", 0x09907, 0x65a3d717, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsirwoodg3, Specsirwoodg3, Spec128)
STD_ROM_FN(Specsirwoodg3)

struct BurnDriver BurnSpecsirwoodg3 = {
	"spec_sirwoodg3", "spec_sirwoodg1", "spec_spec128", NULL, "1990",
	"Sirwood (Part 3 of 3) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsirwoodg3RomInfo, Specsirwoodg3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sito Pons 500cc Grand Prix (Spanish) (128K)

static struct BurnRomInfo SpecsitoponsgpRomDesc[] = {
	{ "Sito Pons 500cc Grand Prix (1990)(Zigurat Software)(es)(128k).z80", 0x09e6f, 0x1f9a9466, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsitoponsgp, Specsitoponsgp, Spec128)
STD_ROM_FN(Specsitoponsgp)

struct BurnDriver BurnSpecsitoponsgp = {
	"spec_sitoponsgp", NULL, "spec_spec128", NULL, "1990",
	"Sito Pons 500cc Grand Prix (Spanish) (128K)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsitoponsgpRomInfo, SpecsitoponsgpRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Skool Daze (48K)

static struct BurnRomInfo SpecskoldazeRomDesc[] = {
	{ "Skool Daze (1985)(Microsphere).z80", 0x0a5f5, 0x4034c78b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specskoldaze, Specskoldaze, Spectrum)
STD_ROM_FN(Specskoldaze)

struct BurnDriver BurnSpecskoldaze = {
	"spec_skoldaze", NULL, "spec_spectrum", NULL, "1985",
	"Skool Daze (48K)\0", NULL, "Microsphere", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecskoldazeRomInfo, SpecskoldazeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Slap Fight (48K)

static struct BurnRomInfo Specslapfigh_48RomDesc[] = {
	{ "Slap Fight (1987)(Imagine Software).z80", 0x09dca, 0xfc50dded, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specslapfigh_48, Specslapfigh_48, Spectrum)
STD_ROM_FN(Specslapfigh_48)

struct BurnDriver BurnSpecslapfigh_48 = {
	"spec_slapfigh_48", "spec_slapfigh", "spec_spectrum", NULL, "1987",
	"Slap Fight (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specslapfigh_48RomInfo, Specslapfigh_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Slap Fight (128K)

static struct BurnRomInfo SpecslapfighRomDesc[] = {
	{ "Slap Fight (1987)(Imagine Software)[128K].z80", 0x0a08e, 0x4b9c236b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specslapfigh, Specslapfigh, Spec128)
STD_ROM_FN(Specslapfigh)

struct BurnDriver BurnSpecslapfigh = {
	"spec_slapfigh", NULL, "spec_spec128", NULL, "1987",
	"Slap Fight (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecslapfighRomInfo, SpecslapfighRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Slightly Magic (48K)

static struct BurnRomInfo Specslightmg_48RomDesc[] = {
	{ "Slightly Magic (1990)(Codemasters).z80", 0x0a76b, 0xd89a3a98, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specslightmg_48, Specslightmg_48, Spectrum)
STD_ROM_FN(Specslightmg_48)

struct BurnDriver BurnSpecslightmg_48 = {
	"spec_slightmg_48", "spec_slightmg", "spec_spectrum", NULL, "1990",
	"Slightly Magic (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specslightmg_48RomInfo, Specslightmg_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Slightly Magic (128K)

static struct BurnRomInfo SpecslightmgRomDesc[] = {
	{ "Slightly Magic (1990)(Codemasters)[128K].z80", 0x11bed, 0x12aaa197, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specslightmg, Specslightmg, Spec128)
STD_ROM_FN(Specslightmg)

struct BurnDriver BurnSpecslightmg = {
	"spec_slightmg", NULL, "spec_spec128", NULL, "1990",
	"Slightly Magic (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecslightmgRomInfo, SpecslightmgRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Smash TV (128K)

static struct BurnRomInfo SpecsmashtvRomDesc[] = {
	{ "Smash TV (1991)(Ocean)[128K].z80", 0x0e6c1, 0x2f90973d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsmashtv, Specsmashtv, Spec128)
STD_ROM_FN(Specsmashtv)

struct BurnDriver BurnSpecsmashtv = {
	"spec_smashtv", NULL, "spec_spec128", NULL, "1991",
	"Smash TV (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsmashtvRomInfo, SpecsmashtvRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Soldier of Fortune (48K)

static struct BurnRomInfo SpecsoldfortRomDesc[] = {
	{ "Soldier Of Fortune (1988)(Firebird).z80", 0x0adff, 0xc3dc26df, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsoldfort, Specsoldfort, Spectrum)
STD_ROM_FN(Specsoldfort)

struct BurnDriver BurnSpecsoldfort = {
	"spec_soldfort", NULL, "spec_spectrum", NULL, "1988",
	"Soldier of Fortune (48K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsoldfortRomInfo, SpecsoldfortRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sol Negro (Spanish) (Part 1 of 2) (128K)

static struct BurnRomInfo Specsolnegro1RomDesc[] = {
	{ "Sol Negro (1989)(Opera Soft)(es)(Part 1 of 2)(128k).z80", 0x09b58, 0x7061e8bc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsolnegro1, Specsolnegro1, Spec128)
STD_ROM_FN(Specsolnegro1)

struct BurnDriver BurnSpecsolnegro1 = {
	"spec_solnegro1", NULL, "spec_spec128", NULL, "1989",
	"Sol Negro (Spanish) (Part 1 of 2) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsolnegro1RomInfo, Specsolnegro1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sol Negro (Spanish) (Part 2 of 2) (128K)

static struct BurnRomInfo Specsolnegro2RomDesc[] = {
	{ "Sol Negro (1989)(Opera Soft)(es)(Part 2 of 2)(128k).z80", 0x0946e, 0x539534b1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsolnegro2, Specsolnegro2, Spec128)
STD_ROM_FN(Specsolnegro2)

struct BurnDriver BurnSpecsolnegro2 = {
	"spec_solnegro2", "spec_solnegro1", "spec_spec128", NULL, "1989",
	"Sol Negro (Spanish) (Part 2 of 2) (128K)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsolnegro2RomInfo, Specsolnegro2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Solomon's Key (48K)

static struct BurnRomInfo SpecsolomonRomDesc[] = {
	{ "Solomon's Key (1987)(U.S. Gold).z80", 0x09608, 0xe9a42bde, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsolomon, Specsolomon, Spectrum)
STD_ROM_FN(Specsolomon)

struct BurnDriver BurnSpecsolomon = {
	"spec_solomon", NULL, "spec_spectrum", NULL, "1987",
	"Solomon's Key (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsolomonRomInfo, SpecsolomonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Space Harrier (48K)

static struct BurnRomInfo SpecsharrierRomDesc[] = {
	{ "Space Harrier (1986)(Elite Systems).z80", 0x0b439, 0xd33b7f51, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsharrier, Specsharrier, Spectrum)
STD_ROM_FN(Specsharrier)

struct BurnDriver BurnSpecsharrier = {
	"spec_sharrier", NULL, "spec_spectrum", NULL, "1986",
	"Space Harrier (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsharrierRomInfo, SpecsharrierRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Space Harrier II (48K)

static struct BurnRomInfo Specsharrir2RomDesc[] = {
	{ "Space Harrier II (1990)(Grandslam Entertainments).z80", 0x0a2e4, 0x2f556b72, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsharrir2, Specsharrir2, Spectrum)
STD_ROM_FN(Specsharrir2)

struct BurnDriver BurnSpecsharrir2 = {
	"spec_sharrir2", NULL, "spec_spectrum", NULL, "1990",
	"Space Harrier II (48K)\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsharrir2RomInfo, Specsharrir2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spy Hunter (48K)

static struct BurnRomInfo SpecspyhuntRomDesc[] = {
	{ "Spy Hunter (1985)(U.S. Gold).z80", 0x086af, 0x7c1b3220, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specspyhunt, Specspyhunt, Spectrum)
STD_ROM_FN(Specspyhunt)

struct BurnDriver BurnSpecspyhunt = {
	"spec_spyhunt", NULL, "spec_spectrum", NULL, "1985",
	"Spy Hunter (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecspyhuntRomInfo, SpecspyhuntRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spy vs Spy (48K)

static struct BurnRomInfo SpecspyvspyRomDesc[] = {
	{ "Spy vs Spy (1985)(Beyond Software).z80", 0x09f9a, 0xa5fc636b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specspyvspy, Specspyvspy, Spectrum)
STD_ROM_FN(Specspyvspy)

struct BurnDriver BurnSpecspyvspy = {
	"spec_spyvspy", NULL, "spec_spectrum", NULL, "1985",
	"Spy vs Spy (48K)\0", NULL, "Beyond Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecspyvspyRomInfo, SpecspyvspyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spy vs Spy II - The Island Caper (48K)

static struct BurnRomInfo Specspyvspy2RomDesc[] = {
	{ "Spy vs Spy II - The Island Caper (1987)(Databyte).z80", 0x09350, 0xe5133176, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specspyvspy2, Specspyvspy2, Spectrum)
STD_ROM_FN(Specspyvspy2)

struct BurnDriver BurnSpecspyvspy2 = {
	"spec_spyvspy2", NULL, "spec_spectrum", NULL, "1987",
	"Spy vs Spy II - The Island Caper (48K)\0", NULL, "Databyte", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specspyvspy2RomInfo, Specspyvspy2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spy vs Spy III - Arctic Antics (48K)

static struct BurnRomInfo Specspyvspy3RomDesc[] = {
	{ "Spy vs Spy III - Arctic Antics (1988)(Databyte).z80", 0x07a46, 0x9bf7db2a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specspyvspy3, Specspyvspy3, Spectrum)
STD_ROM_FN(Specspyvspy3)

struct BurnDriver BurnSpecspyvspy3 = {
	"spec_spyvspy3", NULL, "spec_spectrum", NULL, "1988",
	"Spy vs Spy III - Arctic Antics (48K)\0", NULL, "Databyte", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specspyvspy3RomInfo, Specspyvspy3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Farce (48K)

static struct BurnRomInfo SpecstarfarcRomDesc[] = {
	{ "Star Farce (1988)(Mastertronic).z80", 0x0a888, 0x91817feb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstarfarc, Specstarfarc, Spectrum)
STD_ROM_FN(Specstarfarc)

struct BurnDriver BurnSpecstarfarc = {
	"spec_starfarc", NULL, "spec_spectrum", NULL, "1988",
	"Star Farce (48K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecstarfarcRomInfo, SpecstarfarcRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Starquake (48K)

static struct BurnRomInfo SpecstarquakRomDesc[] = {
	{ "Starquake (1985)(Bubblebus Software).z80", 0x0a40f, 0x0aba61a3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstarquak, Specstarquak, Spectrum)
STD_ROM_FN(Specstarquak)

struct BurnDriver BurnSpecstarquak = {
	"spec_starquak", NULL, "spec_spectrum", NULL, "1985",
	"Starquake (48K)\0", NULL, "Bubblebus Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecstarquakRomInfo, SpecstarquakRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stop the Express (48K)

static struct BurnRomInfo SpecstopexprRomDesc[] = {
	{ "Stop The Express (1983)(Hudson Soft).z80", 0x054c4, 0x56a42e2e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstopexpr, Specstopexpr, Spectrum)
STD_ROM_FN(Specstopexpr)

struct BurnDriver BurnSpecstopexpr = {
	"spec_stopexpr", NULL, "spec_spectrum", NULL, "1983",
	"Stop the Express (48K)\0", NULL, "Sinclair Research", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecstopexprRomInfo, SpecstopexprRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormlord (Trainer)(48K)

static struct BurnRomInfo SpecstormlorRomDesc[] = {
	{ "Stormlord (1989)(Hewson Consultants)[t].z80", 0x09146, 0x95529c30, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstormlor, Specstormlor, Spectrum)
STD_ROM_FN(Specstormlor)

struct BurnDriver BurnSpecstormlor = {
	"spec_stormlor", "spec_stormlor128", "spec_spectrum", NULL, "1989",
	"Stormlord (Trainer)(48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecstormlorRomInfo, SpecstormlorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormlord (128K)

static struct BurnRomInfo Specstormlor128RomDesc[] = {
	{ "Stormlord (1989)(Hewson Consultants)(128k).z80", 0x0ab11, 0x14e2b590, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstormlor128, Specstormlor128, Spec128)
STD_ROM_FN(Specstormlor128)

struct BurnDriver BurnSpecstormlor128 = {
	"spec_stormlor128", NULL, "spec_spec128", NULL, "1989",
	"Stormlord (128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specstormlor128RomInfo, Specstormlor128RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Street Fighter (48K)

static struct BurnRomInfo SpecsfightRomDesc[] = {
	{ "Street Fighter (1983)(Shards Software).z80", 0x04787, 0xd15c56f4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsfight, Specsfight, Spectrum)
STD_ROM_FN(Specsfight)

struct BurnDriver BurnSpecsfight = {
	"spec_sfight", NULL, "spec_spectrum", NULL, "1983",
	"Street Fighter (48K)\0", NULL, "Shards Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsfightRomInfo, SpecsfightRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Strider (128K)

static struct BurnRomInfo SpecstriderRomDesc[] = {
	{ "Strider (1989)(U.S. Gold)[128K].z80", 0x199f5, 0xf7390779, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstrider, Specstrider, Spec128)
STD_ROM_FN(Specstrider)

struct BurnDriver BurnSpecstrider = {
	"spec_strider", NULL, "spec_spec128", NULL, "1989",
	"Strider (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecstriderRomInfo, SpecstriderRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Strider II

static struct BurnRomInfo Specstrider2RomDesc[] = {
	{ "Strider II (1990)(U.S. Gold).tap", 0x21e24, 0xd1b065fc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstrider2, Specstrider2, Spec128)
STD_ROM_FN(Specstrider2)

struct BurnDriver BurnSpecstrider2 = {
	"spec_strider2", NULL, "spec_spec128", NULL, "1990",
	"Strider II\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specstrider2RomInfo, Specstrider2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Striker (128K)

static struct BurnRomInfo SpecstrikerRomDesc[] = {
	{ "Striker (1989)(Cult Games)[128K].z80", 0x090e9, 0xf7144c4d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstriker, Specstriker, Spec128)
STD_ROM_FN(Specstriker)

struct BurnDriver BurnSpecstriker = {
	"spec_striker", NULL, "spec_spec128", NULL, "1989",
	"Striker (128K)\0", NULL, "Cult Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecstrikerRomInfo, SpecstrikerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stunt Car Racer (48K)

static struct BurnRomInfo Specstuntcar_48RomDesc[] = {
	{ "Stunt Car Racer (1989)(Micro Style).z80", 0x08f77, 0xc02a3c1f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstuntcar_48, Specstuntcar_48, Spectrum)
STD_ROM_FN(Specstuntcar_48)

struct BurnDriver BurnSpecstuntcar_48 = {
	"spec_stuntcar_48", "spec_stuntcar", "spec_spectrum", NULL, "1989",
	"Stunt Car Racer (48K)\0", NULL, "Micro Style", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specstuntcar_48RomInfo, Specstuntcar_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stunt Car Racer (128K)

static struct BurnRomInfo SpecstuntcarRomDesc[] = {
	{ "Stunt Car Racer (1989)(Micro Style)[128K].z80", 0x0ae97, 0xb3abb3a6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstuntcar, Specstuntcar, Spec128)
STD_ROM_FN(Specstuntcar)

struct BurnDriver BurnSpecstuntcar = {
	"spec_stuntcar", NULL, "spec_spec128", NULL, "1989",
	"Stunt Car Racer (128K)\0", NULL, "Micro Style", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecstuntcarRomInfo, SpecstuntcarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Subbuteo - The Computer Game (48K)

static struct BurnRomInfo Specsubbueto_48RomDesc[] = {
	{ "Subbuteo - The Computer Game (1990)(Electronic Zoo).z80", 0x083de, 0x31346a92, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsubbueto_48, Specsubbueto_48, Spectrum)
STD_ROM_FN(Specsubbueto_48)

struct BurnDriver BurnSpecsubbueto_48 = {
	"spec_subbueto_48", "spec_subbueto", "spec_spectrum", NULL, "1990",
	"Subbuteo - The Computer Game (48K)\0", NULL, "Electronic Zoo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specsubbueto_48RomInfo, Specsubbueto_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Subbuteo - The Computer Game (128K)

static struct BurnRomInfo SpecsubbuetoRomDesc[] = {
	{ "Subbuteo - The Computer Game (1990)(Electronic Zoo)[128K].z80", 0x0eb1e, 0x37c72d4c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsubbueto, Specsubbueto, Spec128)
STD_ROM_FN(Specsubbueto)

struct BurnDriver BurnSpecsubbueto = {
	"spec_subbueto", NULL, "spec_spec128", NULL, "1990",
	"Subbuteo - The Computer Game (128K)\0", NULL, "Electronic Zoo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsubbuetoRomInfo, SpecsubbuetoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Hang-On - Part 1 - Beginner (48K)

static struct BurnRomInfo SpecshangonRomDesc[] = {
	{ "Super Hang-On - Part 1 - Beginner (1986)(Electric Dreams Software).z80", 0x08818, 0x6021b420, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshangon, Specshangon, Spectrum)
STD_ROM_FN(Specshangon)

struct BurnDriver BurnSpecshangon = {
	"spec_shangon", NULL, "spec_spectrum", NULL, "1986",
	"Super Hang-On - Part 1 - Beginner (48K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecshangonRomInfo, SpecshangonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Hang-On - Part 2 - Junior (48K)

static struct BurnRomInfo Specshangon2RomDesc[] = {
	{ "Super Hang-On - Part 2 - Junior (1986)(Electric Dreams Software).z80", 0x08690, 0xd8180d70, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshangon2, Specshangon2, Spectrum)
STD_ROM_FN(Specshangon2)

struct BurnDriver BurnSpecshangon2 = {
	"spec_shangon2", "spec_shangon", "spec_spectrum", NULL, "1986",
	"Super Hang-On - Part 2 - Junior (48K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specshangon2RomInfo, Specshangon2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Hang-On - Part 3 - Senior (48K)

static struct BurnRomInfo Specshangon3RomDesc[] = {
	{ "Super Hang-On - Part 3 - Senior (1986)(Electric Dreams Software).z80", 0x08adc, 0x363567ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshangon3, Specshangon3, Spectrum)
STD_ROM_FN(Specshangon3)

struct BurnDriver BurnSpecshangon3 = {
	"spec_shangon3", "spec_shangon", "spec_spectrum", NULL, "1986",
	"Super Hang-On - Part 3 - Senior (48K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specshangon3RomInfo, Specshangon3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Hang-On - Part 4 - Expert (48K)

static struct BurnRomInfo Specshangon4RomDesc[] = {
	{ "Super Hang-On - Part 4 - Expert (1986)(Electric Dreams Software).z80", 0x0868a, 0x8cb2ac52, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshangon4, Specshangon4, Spectrum)
STD_ROM_FN(Specshangon4)

struct BurnDriver BurnSpecshangon4 = {
	"spec_shangon4", "spec_shangon", "spec_spectrum", NULL, "1986",
	"Super Hang-On - Part 4 - Expert (48K)\0", NULL, "Electric Dreams Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specshangon4RomInfo, Specshangon4RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Monaco GP (128K)

static struct BurnRomInfo SpecsmgpRomDesc[] = {
	{ "Super Monaco GP (1991)(U.S. Gold)[128K].z80", 0x0e08c, 0x6a1dcc87, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsmgp, Specsmgp, Spec128)
STD_ROM_FN(Specsmgp)

struct BurnDriver BurnSpecsmgp = {
	"spec_smgp", NULL, "spec_spec128", NULL, "1991",
	"Super Monaco GP (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsmgpRomInfo, SpecsmgpRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Sprint (48K)

static struct BurnRomInfo SpecsupespriRomDesc[] = {
	{ "Super Sprint (1987)(Activision).z80", 0x07a7e, 0x52ee2754, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsupespri, Specsupespri, Spectrum)
STD_ROM_FN(Specsupespri)

struct BurnDriver BurnSpecsupespri = {
	"spec_supespri", NULL, "spec_spectrum", NULL, "1987",
	"Super Sprint (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsupespriRomInfo, SpecsupespriRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// SWIV (128K)

static struct BurnRomInfo SpecswivRomDesc[] = {
	{ "SWIV (1991)(Storm Software).tap", 0x2378e, 0xbaf9be65, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specswiv, Specswiv, Spec128)
STD_ROM_FN(Specswiv)

struct BurnDriver BurnSpecswiv = {
	"spec_swiv", NULL, "spec_spec128", NULL, "1991",
	"SWIV (128K)\0", NULL, "Storm Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecswivRomInfo, SpecswivRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// SWIV (Block1)(128K)

static struct BurnRomInfo Specswivblk1RomDesc[] = {
	{ "SWIV (1991)(Storm Software)(Block1)(128k).z80", 0x19673, 0x92a3c4e7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specswivblk1, Specswivblk1, Spec128)
STD_ROM_FN(Specswivblk1)

struct BurnDriver BurnSpecswivblk1 = {
	"spec_swivblk1", "spec_swiv", "spec_spec128", NULL, "1991",
	"SWIV (Block1)(128K)\0", NULL, "Storm Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 2, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, Specswivblk1RomInfo, Specswivblk1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Technician Ted (48K)

static struct BurnRomInfo Spectechted_48RomDesc[] = {
	{ "Technician Ted (1984)(Hewson Consultants).z80", 0x0a2af, 0x90e4eaee, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectechted_48, Spectechted_48, Spectrum)
STD_ROM_FN(Spectechted_48)

struct BurnDriver BurnSpectechted_48 = {
	"spec_techted_48", NULL, "spec_spectrum", NULL, "1984",
	"Technician Ted (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectechted_48RomInfo, Spectechted_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Teenage Mutant Hero Turtles - The Coin-Op (48K)

static struct BurnRomInfo SpectmhtarcRomDesc[] = {
	{ "Teenage Mutant Hero Turtles - The Coin-Op (1991)(Image Works).z80", 0x0a173, 0x2adc23b7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectmhtarc, Spectmhtarc, Spectrum)
STD_ROM_FN(Spectmhtarc)

struct BurnDriver BurnSpectmhtarc = {
	"spec_tmhtarc", NULL, "spec_spectrum", NULL, "1991",
	"Teenage Mutant Hero Turtles - The Coin-Op (48K)\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectmhtarcRomInfo, SpectmhtarcRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Teenage Mutant Hero Turtles (48K)

static struct BurnRomInfo Spectmht_48RomDesc[] = {
	{ "Teenage Mutant Hero Turtles (1990)(Image Works).z80", 0x0b51c, 0xfa454654, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectmht_48, Spectmht_48, Spectrum)
STD_ROM_FN(Spectmht_48)

struct BurnDriver BurnSpectmht_48 = {
	"spec_tmht_48", "spec_tmht", "spec_spectrum", NULL, "1990",
	"Teenage Mutant Hero Turtles (48K)\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectmht_48RomInfo, Spectmht_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Teenage Mutant Hero Turtles (128K)

static struct BurnRomInfo SpectmhtRomDesc[] = {
	{ "Teenage Mutant Hero Turtles (1990)(Image Works)[128K].z80", 0x0c0b6, 0x66d86001, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectmht, Spectmht, Spec128)
STD_ROM_FN(Spectmht)

struct BurnDriver BurnSpectmht = {
	"spec_tmht", NULL, "spec_spec128", NULL, "1990",
	"Teenage Mutant Hero Turtles (128K)\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectmhtRomInfo, SpectmhtRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Terra Cresta (48K)

static struct BurnRomInfo Specterracrs_48RomDesc[] = {
	{ "Terra Cresta (1986)(Imagine Software).z80", 0x08ff2, 0xa28b1755, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specterracrs_48, Specterracrs_48, Spectrum)
STD_ROM_FN(Specterracrs_48)

struct BurnDriver BurnSpecterracrs_48 = {
	"spec_terracrs_48", NULL, "spec_spectrum", NULL, "1986",
	"Terra Cresta (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specterracrs_48RomInfo, Specterracrs_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tetris (Mirrorsoft) (48K)

static struct BurnRomInfo Spectetris_48RomDesc[] = {
	{ "Tetris (1988)(Mirrorsoft).z80", 0x06c7a, 0x1226181e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectetris_48, Spectetris_48, Spectrum)
STD_ROM_FN(Spectetris_48)

struct BurnDriver BurnSpectetris_48 = {
	"spec_tetris_48", "spec_tetris", "spec_spectrum", NULL, "1988",
	"Tetris (Mirrorsoft) (48K)\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectetris_48RomInfo, Spectetris_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tetris (Mirrorsoft) (128K)

static struct BurnRomInfo SpectetrisRomDesc[] = {
	{ "Tetris (1988)(Mirrorsoft)(128k).z80", 0x075b1, 0xca758c04, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectetris, Spectetris, Spec128)
STD_ROM_FN(Spectetris)

struct BurnDriver BurnSpectetris = {
	"spec_tetris", NULL, "spec_spec128", NULL, "1988",
	"Tetris (Mirrorsoft) (128K)\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectetrisRomInfo, SpectetrisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Three Weeks in Paradise (48K)

static struct BurnRomInfo Spec3weekspr_48RomDesc[] = {
	{ "Three Weeks In Paradise (1986)(Mikro-Gen).z80", 0x0a49e, 0x3c7ac0a9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec3weekspr_48, Spec3weekspr_48, Spectrum)
STD_ROM_FN(Spec3weekspr_48)

struct BurnDriver BurnSpec3weekspr_48 = {
	"spec_3weekspr_48", "spec_3weekspr", "spec_spectrum", NULL, "1985",
	"Three Weeks in Paradise (48K)\0", NULL, "Mikro-Gen", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec3weekspr_48RomInfo, Spec3weekspr_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Three Weeks in Paradise (128K)

static struct BurnRomInfo Spec3weeksprRomDesc[] = {
	{ "Three Weeks In Paradise (1986)(Mikro-Gen)(128k).z80", 0x0e06c, 0xf21d8b5d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec3weekspr, Spec3weekspr, Spec128)
STD_ROM_FN(Spec3weekspr)

struct BurnDriver BurnSpec3weekspr = {
	"spec_3weekspr", NULL, "spec_spec128", NULL, "1985",
	"Three Weeks in Paradise (128K)\0", NULL, "Mikro-Gen", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec3weeksprRomInfo, Spec3weeksprRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thunder Blade (128K)

static struct BurnRomInfo SpecthndrbldRomDesc[] = {
	{ "Thunder Blade (1988)(U.S. Gold)[128K].z80", 0x09abe, 0xb6773249, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specthndrbld, Specthndrbld, Spec128)
STD_ROM_FN(Specthndrbld)

struct BurnDriver BurnSpecthndrbld = {
	"spec_thndrbld", NULL, "spec_spec128", NULL, "1988",
	"Thunder Blade (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecthndrbldRomInfo, SpecthndrbldRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tiger Road (48K)

static struct BurnRomInfo Spectigeroad_48RomDesc[] = {
	{ "Tiger Road (1988)(Go!).z80", 0x09f9b, 0x04767cb9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectigeroad_48, Spectigeroad_48, Spectrum)
STD_ROM_FN(Spectigeroad_48)

struct BurnDriver BurnSpectigeroad_48 = {
	"spec_tigeroad_48", "spec_tigeroad", "spec_spectrum", NULL, "1988",
	"Tiger Road (48K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectigeroad_48RomInfo, Spectigeroad_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tiger Road (128K)

static struct BurnRomInfo SpectigeroadRomDesc[] = {
	{ "Tiger Road (1988)(Go!)[128K].z80", 0x0b726, 0xede04afd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectigeroad, Spectigeroad, Spec128)
STD_ROM_FN(Spectigeroad)

struct BurnDriver BurnSpectigeroad = {
	"spec_tigeroad", NULL, "spec_spec128", NULL, "1988",
	"Tiger Road (128K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectigeroadRomInfo, SpectigeroadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Time Scanner (48K)

static struct BurnRomInfo SpectimescanRomDesc[] = {
	{ "Time Scanner (1989)(Activision).z80", 0x0a46a, 0x06983d6e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectimescan, Spectimescan, Spectrum)
STD_ROM_FN(Spectimescan)

struct BurnDriver BurnSpectimescan = {
	"spec_timescan", NULL, "spec_spectrum", NULL, "1989",
	"Time Scanner (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectimescanRomInfo, SpectimescanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Titanic (Topo Soft) (Part 1 of 2) (128K)

static struct BurnRomInfo Spectitanictopo1RomDesc[] = {
	{ "Titanic (1988)(Topo Soft)(Part 1 of 2)(128k).z80", 0x09571, 0xc91e3859, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectitanictopo1, Spectitanictopo1, Spec128)
STD_ROM_FN(Spectitanictopo1)

struct BurnDriver BurnSpectitanictopo1 = {
	"spec_titanictopo1", NULL, "spec_spec128", NULL, "1988",
	"Titanic (Topo Soft) (Part 1 of 2) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectitanictopo1RomInfo, Spectitanictopo1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Titanic (Topo Soft) (Part 2 of 2) (128K)

static struct BurnRomInfo Spectitanictopo2RomDesc[] = {
	{ "Titanic (1988)(Topo Soft)(Part 2 of 2)(128k).z80", 0x09f96, 0x138225dd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectitanictopo2, Spectitanictopo2, Spec128)
STD_ROM_FN(Spectitanictopo2)

struct BurnDriver BurnSpectitanictopo2 = {
	"spec_titanictopo2", "spec_titanictopo1", "spec_spec128", NULL, "1988",
	"Titanic (Topo Soft) (Part 2 of 2) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectitanictopo2RomInfo, Spectitanictopo2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Total Recall (128K)

static struct BurnRomInfo SpectotrcallRomDesc[] = {
	{ "Total Recall (1991)(Ocean)[128K].z80", 0x17197, 0xab3503be, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectotrcall, Spectotrcall, Spec128)
STD_ROM_FN(Spectotrcall)

struct BurnDriver BurnSpectotrcall = {
	"spec_totrcall", NULL, "spec_spec128", NULL, "1991",
	"Total Recall (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectotrcallRomInfo, SpectotrcallRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Track and Field (48K)

static struct BurnRomInfo SpectracfielRomDesc[] = {
	{ "Track and Field (1988)(Ocean).z80", 0x05787, 0x7ddee010, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectracfiel, Spectracfiel, Spectrum)
STD_ROM_FN(Spectracfiel)

struct BurnDriver BurnSpectracfiel = {
	"spec_tracfiel", NULL, "spec_spectrum", NULL, "1988",
	"Track and Field (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectracfielRomInfo, SpectracfielRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Trantor - The Last Stormtrooper (48K)

static struct BurnRomInfo SpectrthlastRomDesc[] = {
	{ "Trantor The Last Stormtrooper (1987)(Go!).z80", 0x09b69, 0xbad682da, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectrthlast, Spectrthlast, Spectrum)
STD_ROM_FN(Spectrthlast)

struct BurnDriver BurnSpectrthlast = {
	"spec_trthlast", NULL, "spec_spectrum", NULL, "1987",
	"Trantor - The Last Stormtrooper (48K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectrthlastRomInfo, SpectrthlastRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Trap Door, The (48K)

static struct BurnRomInfo SpectradoothRomDesc[] = {
	{ "Trap Door, The (1986)(Piranha).z80", 0x0a73f, 0x27e0667f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectradooth, Spectradooth, Spectrum)
STD_ROM_FN(Spectradooth)

struct BurnDriver BurnSpectradooth = {
	"spec_tradooth", NULL, "spec_spectrum", NULL, "1986",
	"Trap Door, The (48K)\0", NULL, "Piranha", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectradoothRomInfo, SpectradoothRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Underwurlde (48K)

static struct BurnRomInfo SpecunderwurRomDesc[] = {
	{ "Underwurlde (1984)(Ultimate Play The Game).z80", 0x0979b, 0xbffc6f35, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specunderwur, Specunderwur, Spectrum)
STD_ROM_FN(Specunderwur)

struct BurnDriver BurnSpecunderwur = {
	"spec_underwur", NULL, "spec_spectrum", NULL, "1984",
	"Underwurlde (48K)\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecunderwurRomInfo, SpecunderwurRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Untouchables, The (48K)

static struct BurnRomInfo SpecuntouchbRomDesc[] = {
	{ "Untouchables, The (1989)(Ocean)[needs tape load].z80", 0x09cea, 0x98cc8b4f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specuntouchb, Specuntouchb, Spectrum)
STD_ROM_FN(Specuntouchb)

struct BurnDriver BurnSpecuntouchb = {
	"spec_untouchb", NULL, "spec_spectrum", NULL, "1989",
	"Untouchables, The (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecuntouchbRomInfo, SpecuntouchbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Valley of Rains (128k)

static struct BurnRomInfo SpecvalleyofrainsRomDesc[] = {
	{ "Valley of Rains (2019)(Zosya Entertainment)(128k).z80", 0x0b647, 0x7a79ca09, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specvalleyofrains, Specvalleyofrains, Spec128)
STD_ROM_FN(Specvalleyofrains)

struct BurnDriver BurnSpecvalleyofrains = {
	"spec_valleyofrains", NULL, "spec_spec128", NULL, "2019",
	"Valley of Rains (128k)\0", NULL, "Zosya Entertainment", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecvalleyofrainsRomInfo, SpecvalleyofrainsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vindicator, The (128K)

static struct BurnRomInfo SpecvindtheRomDesc[] = {
	{ "Vindicator, The (1988)(Imagine Software)[128K].z80", 0x1b692, 0x57c8a81d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specvindthe, Specvindthe, Spec128)
STD_ROM_FN(Specvindthe)

struct BurnDriver BurnSpecvindthe = {
	"spec_vindthe", NULL, "spec_spec128", NULL, "1988",
	"Vindicator, The (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecvindtheRomInfo, SpecvindtheRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vindicators (128K)

static struct BurnRomInfo SpecvindicatRomDesc[] = {
	{ "Vindicators (1989)(Domark).tap", 0x0ffc4, 0xc4b6502e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specvindicat, Specvindicat, Spec128)
STD_ROM_FN(Specvindicat)

struct BurnDriver BurnSpecvindicat = {
	"spec_vindicat", NULL, "spec_spec128", NULL, "1989",
	"Vindicators (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecvindicatRomInfo, SpecvindicatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wacky Darts (48K)

static struct BurnRomInfo Specwackdarts_48RomDesc[] = {
	{ "Wacky Darts (1991)(Codemasters)(48K).z80", 0x0ae9d, 0x6214a4ce, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwackdarts_48, Specwackdarts_48, Spectrum)
STD_ROM_FN(Specwackdarts_48)

struct BurnDriver BurnSpecwackdart_48 = {
	"spec_wackdarts_48", "spec_wackydarts", "spec_spectrum", NULL, "1991",
	"Wacky Darts (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specwackdarts_48RomInfo, Specwackdarts_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wacky Races (Trainer)(128K)

static struct BurnRomInfo SpecwackraceRomDesc[] = {
	{ "Wacky Races (1992)(Hi-Tec Software)(128k).z80", 0x16523, 0xee205ebd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwackrace, Specwackrace, Spec128)
STD_ROM_FN(Specwackrace)

struct BurnDriver BurnSpecwackrace = {
	"spec_wackrace", "spec_wackracestd", "spec_spec128", NULL, "1992",
	"Wacky Races (Trainer)(128K)\0", NULL, "Hi-Tec Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecwackraceRomInfo, SpecwackraceRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wacky Races (Standard)(128K)

static struct BurnRomInfo SpecwackracestdRomDesc[] = {
	{ "Wacky Races (1992)(Hi-Tec Software)(Standard)(128k).z80", 0x15f5d, 0xc78a1639, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwackracestd, Specwackracestd, Spec128)
STD_ROM_FN(Specwackracestd)

struct BurnDriver BurnSpecwackracestd = {
	"spec_wackracestd", NULL, "spec_spec128", NULL, "1992",
	"Wacky Races (Standard)(128K)\0", NULL, "Hi-Tec Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecwackracestdRomInfo, SpecwackracestdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wanted! Monty Mole (48K)

static struct BurnRomInfo SpecwanmonmoRomDesc[] = {
	{ "Monty Mole (1984)(Gremlin Graphics).z80", 0x09c78, 0x2e7a94b2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwanmonmo, Specwanmonmo, Spectrum)
STD_ROM_FN(Specwanmonmo)

struct BurnDriver BurnSpecwanmonmo = {
	"spec_wanmonmo", NULL, "spec_spectrum", NULL, "1984",
	"Wanted! Monty Mole (48K)\0", NULL, "Gremlin Graphics Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecwanmonmoRomInfo, SpecwanmonmoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Way of the Exploding Fist, The (128K)

static struct BurnRomInfo SpecwayexplfRomDesc[] = {
	{ "Way of the Exploding Fist, The (1985)(Melbourne House)(128k).z80", 0x0aec2, 0x8b68b37c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwayexplf, Specwayexplf, Spec128)
STD_ROM_FN(Specwayexplf)

struct BurnDriver BurnSpecwayexplf = {
	"spec_wayexplf", NULL, "spec_spec128", NULL, "1985",
	"Way of the Exploding Fist, The (128K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecwayexplfRomInfo, SpecwayexplfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// WEC Le Mans (48K)

static struct BurnRomInfo Specwecleman_48RomDesc[] = {
	{ "WEC Le Mans (1988)(Imagine Software).z80", 0x0a23e, 0x83a8e1f7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwecleman_48, Specwecleman_48, Spectrum)
STD_ROM_FN(Specwecleman_48)

struct BurnDriver BurnSpecwecleman_48 = {
	"spec_wecleman_48", "spec_wecleman", "spec_spectrum", NULL, "1988",
	"WEC Le Mans (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specwecleman_48RomInfo, Specwecleman_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// WEC Le Mans (128K)

static struct BurnRomInfo SpecweclemanRomDesc[] = {
	{ "WEC Le Mans (1988)(Imagine Software)[128K].z80", 0x0d90e, 0xa77096e2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwecleman, Specwecleman, Spec128)
STD_ROM_FN(Specwecleman)

struct BurnDriver BurnSpecwecleman = {
	"spec_wecleman", NULL, "spec_spec128", NULL, "1988",
	"WEC Le Mans (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecweclemanRomInfo, SpecweclemanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wild West Seymour (48K)

static struct BurnRomInfo Specwwseymr_48RomDesc[] = {
	{ "Wild West Seymour (1992)(Codemasters).z80", 0x0b4ab, 0xbf324d91, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwwseymr_48, Specwwseymr_48, Spectrum)
STD_ROM_FN(Specwwseymr_48)

struct BurnDriver BurnSpecwwseymr_48 = {
	"spec_wwseymr_48", "spec_wwseymr", "spec_spectrum", NULL, "1992",
	"Wild West Seymour (48K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specwwseymr_48RomInfo, Specwwseymr_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wild West Seymour (128K)

static struct BurnRomInfo SpecwwseymrRomDesc[] = {
	{ "Wild West Seymour (1992)(Codemasters)(128k).z80", 0x141c6, 0x01dc3cee, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwwseymr, Specwwseymr, Spec128)
STD_ROM_FN(Specwwseymr)

struct BurnDriver BurnSpecwwseymr = {
	"spec_wwseymr", NULL, "spec_spec128", NULL, "1992",
	"Wild West Seymour (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecwwseymrRomInfo, SpecwwseymrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wonder Boy (128K)

static struct BurnRomInfo SpecwboyRomDesc[] = {
	{ "Wonder Boy (1987)(Activision)[128K].z80", 0x11193, 0xb492a055, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwboy, Specwboy, Spec128)
STD_ROM_FN(Specwboy)

struct BurnDriver BurnSpecwboy = {
	"spec_wboy", NULL, "spec_spec128", NULL, "1987",
	"Wonder Boy (128K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecwboyRomInfo, SpecwboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Xenon (48K)

static struct BurnRomInfo SpecxenonRomDesc[] = {
	{ "Xenon (1988)(Melbourne House).z80", 0x09577, 0x889e59ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specxenon, Specxenon, Spectrum)
STD_ROM_FN(Specxenon)

struct BurnDriver BurnSpecxenon = {
	"spec_xenon", NULL, "spec_spectrum", NULL, "1988",
	"Xenon (48K)\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecxenonRomInfo, SpecxenonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Xevious (48K)

static struct BurnRomInfo SpecxeviousRomDesc[] = {
	{ "Xevious (1987)(U.S. Gold).z80", 0x05ae7, 0xac2c0235, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specxevious, Specxevious, Spectrum)
STD_ROM_FN(Specxevious)

struct BurnDriver BurnSpecxevious = {
	"spec_xevious", NULL, "spec_spectrum", NULL, "1987",
	"Xevious (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecxeviousRomInfo, SpecxeviousRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yie Ar Kung-Fu (48K)

static struct BurnRomInfo Specyiarkufu_48RomDesc[] = {
	{ "Yie Ar Kung-Fu (1985)(Imagine Software).z80", 0x0a130, 0x23da9f6d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specyiarkufu_48, Specyiarkufu_48, Spectrum)
STD_ROM_FN(Specyiarkufu_48)

struct BurnDriver BurnSpecyiarkufu_48 = {
	"spec_yiarkufu_48", "spec_yiarkufu", "spec_spectrum", NULL, "1985",
	"Yie Ar Kung-Fu (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specyiarkufu_48RomInfo, Specyiarkufu_48RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yie Ar Kung-Fu (128K)

static struct BurnRomInfo SpecyiarkufuRomDesc[] = {
	{ "Yie Ar Kung-Fu (1985)(Imagine Software)[128K].z80", 0x157f7, 0xf7c52002, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specyiarkufu, Specyiarkufu, Spec128)
STD_ROM_FN(Specyiarkufu)

struct BurnDriver BurnSpecyiarkufu = {
	"spec_yiarkufu", NULL, "spec_spec128", NULL, "1985",
	"Yie Ar Kung-Fu (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecyiarkufuRomInfo, SpecyiarkufuRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yie Ar Kung-Fu II (48K)

static struct BurnRomInfo Specyiarkuf2RomDesc[] = {
	{ "Yie Ar Kung-Fu II (1986)(Imagine Software).z80", 0x08f6e, 0xef420fe9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specyiarkuf2, Specyiarkuf2, Spectrum)
STD_ROM_FN(Specyiarkuf2)

struct BurnDriver BurnSpecyiarkuf2 = {
	"spec_yiarkuf2", NULL, "spec_spectrum", NULL, "1986",
	"Yie Ar Kung-Fu II (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specyiarkuf2RomInfo, Specyiarkuf2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Zaxxon (48K)

static struct BurnRomInfo SpeczaxxonRomDesc[] = {
	{ "Zaxxon (1985)(U.S. Gold).z80", 0x092e6, 0xe89d7896, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speczaxxon, Speczaxxon, Spectrum)
STD_ROM_FN(Speczaxxon)

struct BurnDriver BurnSpeczaxxon = {
	"spec_zaxxon", NULL, "spec_spectrum", NULL, "1985",
	"Zaxxon (48K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeczaxxonRomInfo, SpeczaxxonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lorna

static struct BurnRomInfo SpecLornaRomDesc[] = {
	{ "Lorna (1990)(Topo Soft)(Sp).tap", 0x1cf8a, 0xa09afe9a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLorna, SpecLorna, Spec128)
STD_ROM_FN(SpecLorna)

struct BurnDriver BurnSpecLorna = {
	"spec_lorna", NULL, "spec_spec128", NULL, "1990",
	"Lorna (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecLornaRomInfo, SpecLornaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInvesInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Myth

static struct BurnRomInfo SpecMythRomDesc[] = {
	{ "Myth - History In The Making (1989)(System 3 Software).tap", 0x2f76e, 0xbc8f2fe4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMyth, SpecMyth, Spec128)
STD_ROM_FN(SpecMyth)

struct BurnDriver BurnSpecMyth = {
	"spec_myth", NULL, "spec_spec128", NULL, "1989",
	"Myth - History In The Making (128K)\0", NULL, "System 3 Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMythRomInfo, SpecMythRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gremlins 2

static struct BurnRomInfo SpecGremlins2RomDesc[] = {
	{ "Gremlins 2 - The New Batch (1990)(Topo Soft - Elite Systems).tap", 0x2716c, 0xb1402dfe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGremlins2, SpecGremlins2, Spec128)
STD_ROM_FN(SpecGremlins2)

struct BurnDriver BurnSpecGremlins2 = {
	"spec_gremlins2", NULL, "spec_spec128", NULL, "1990",
	"Gremlins 2 - The New Batch (128K)\0", NULL, "Topo Soft - Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGremlins2RomInfo, SpecGremlins2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Narco Police

static struct BurnRomInfo SpecNarcopolRomDesc[] = {
	{ "Narco Police (1991)(Rajsoft).tap", 0x1cf59, 0xd47256a3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNarcopol, SpecNarcopol, Spec128)
STD_ROM_FN(SpecNarcopol)

struct BurnDriver BurnSpecNarcopol = {
	"spec_narcopol", NULL, "spec_spec128", NULL, "1991",
	"Narco Police (128K)\0", NULL, "Rajsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecNarcopolRomInfo, SpecNarcopolRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// OutRun

static struct BurnRomInfo SpecOutrunRomDesc[] = {
	{ "Outrun (1987)(US Gold).tap", 0x1f5ca, 0x1c4dde6b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOutrun, SpecOutrun, Spec128)
STD_ROM_FN(SpecOutrun)

struct BurnDriver BurnSpecOutrun = {
	"spec_outrun", NULL, "spec_spec128", NULL, "1987",
	"Outrun (128K)\0", NULL, "US Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecOutrunRomInfo, SpecOutrunRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Outrun Europa

static struct BurnRomInfo SpecOutruneuRomDesc[] = {
	{ "Outrun Europa (1991)(US Gold).tap", 0x495d0, 0x3240887e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOutruneu, SpecOutruneu, Spec128)
STD_ROM_FN(SpecOutruneu)

struct BurnDriver BurnSpecOutruneu = {
	"spec_outruneu", NULL, "spec_spec128", NULL, "1991",
	"Outrun Europa (128K)\0", NULL, "US Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecOutruneuRomInfo, SpecOutruneuRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// New Zealand Story, The

static struct BurnRomInfo SpecTnzsRomDesc[] = {
	{ "New Zealand Story, The (1989)(Ocean Software).tap", 0x2b800, 0xe6f32d53, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTnzs, SpecTnzs, Spec128)
STD_ROM_FN(SpecTnzs)

struct BurnDriver BurnSpecTnzs = {
	"spec_tnzs", NULL, "spec_spec128", NULL, "1989",
	"New Zealand Story, The (128K)\0", NULL, "Ocean Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTnzsRomInfo, SpecTnzsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Turbo Outrun

static struct BurnRomInfo SpecToutrunRomDesc[] = {
	{ "Turbo Outrun (1990)(US Gold).tap", 0x5a75a, 0x80a6916d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecToutrun, SpecToutrun, Spec128)
STD_ROM_FN(SpecToutrun)

struct BurnDriver BurnSpecToutrun = {
	"spec_toutrun", NULL, "spec_spec128", NULL, "1990",
	"Turbo Outrun (128K)\0", NULL, "US Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecToutrunRomInfo, SpecToutrunRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Astro Marine Corps (Spanish) (Part 1 of 2)

static struct BurnRomInfo SpecastmarcsRomDesc[] = {
	{ "Astro Marine Corps (1989)(Dinamic Software)(es)(Part 1 of 2).z80", 0x0c38f, 0x643f916a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specastmarcs, Specastmarcs, Spec128)
STD_ROM_FN(Specastmarcs)

struct BurnDriver BurnSpecastmarcs = {
	"spec_astmarcs", NULL, "spec_spec128", NULL, "1989",
	"Astro Marine Corps (Spanish) (Part 1 of 2)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecastmarcsRomInfo, SpecastmarcsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Astro Marine Corps (Spanish) (Part 2 of 2)

static struct BurnRomInfo Specastmarcs2RomDesc[] = {
	{ "Astro Marine Corps (1989)(Dinamic Software)(es)(Part 2 of 2).z80", 0x0bcce, 0x295074fd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specastmarcs2, Specastmarcs2, Spec128)
STD_ROM_FN(Specastmarcs2)

struct BurnDriver BurnSpecastmarcs2 = {
	"spec_astmarcs2", "spec_astmarcs", "spec_spec128", NULL, "1989",
	"Astro Marine Corps (Spanish) (Part 2 of 2)\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specastmarcs2RomInfo, Specastmarcs2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Choy-Lee-Fut Kung-Fu Warrior (Spanish)

static struct BurnRomInfo SpecclfkfwRomDesc[] = {
	{ "Choy-Lee-Fut Kung-Fu Warrior (1990)(Positive)(es).tap", 0x0bf47, 0x4af1bacf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specclfkfw, Specclfkfw, Spec128)
STD_ROM_FN(Specclfkfw)

struct BurnDriver BurnSpecclfkfw = {
	"spec_clfkfw", NULL, "spec_spec128", NULL, "1990",
	"Choy-Lee-Fut Kung-Fu Warrior (Spanish)\0", NULL, "Positive", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecclfkfwRomInfo, SpecclfkfwRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Crosswize

static struct BurnRomInfo SpeccrosswizeRomDesc[] = {
	{ "Crosswize (1988)(Firebird Software).tap", 0x0c4e7, 0x8a6d909b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccrosswize, Speccrosswize, Spec128)
STD_ROM_FN(Speccrosswize)

struct BurnDriver BurnSpeccrosswize = {
	"spec_crosswize", NULL, "spec_spec128", NULL, "1988",
	"Crosswize\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeccrosswizeRomInfo, SpeccrosswizeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Goody (Spanish)

static struct BurnRomInfo SpecgoodyesRomDesc[] = {
	{ "Goody (1987)(Opera Soft)(es).tap", 0x0bdae, 0x2fd4f11b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgoodyes, Specgoodyes, Spec128)
STD_ROM_FN(Specgoodyes)

struct BurnDriver BurnSpecgoodyes = {
	"spec_goodyes", NULL, "spec_spec128", NULL, "1987",
	"Goody (Spanish)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgoodyesRomInfo, SpecgoodyesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Goody (Recoloured)(Spanish)

static struct BurnRomInfo SpecgoodyrecolourRomDesc[] = {
	{ "Goody (1987)(Opera Soft)(Recoloured)(es).tap", 0x0dbb2, 0x8b02e960, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgoodyrecolour, Specgoodyrecolour, Spec128)
STD_ROM_FN(Specgoodyrecolour)

struct BurnDriver BurnSpecgoodyrecolour = {
	"spec_goodyrecolour", NULL, "spec_spec128", NULL, "1987",
	"Goody (Recoloured)(Spanish)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgoodyrecolourRomInfo, SpecgoodyrecolourRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ice Breaker (Spanish)

static struct BurnRomInfo SpecicebreakerRomDesc[] = {
	{ "Ice Breaker (1990)(Topo Soft)(es).tap", 0x127d5, 0x62ce5b80, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specicebreaker, Specicebreaker, Spec128)
STD_ROM_FN(Specicebreaker)

struct BurnDriver BurnSpecicebreaker = {
	"spec_icebreaker", NULL, "spec_spec128", NULL, "1990",
	"Ice Breaker (Spanish)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecicebreakerRomInfo, SpecicebreakerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// International Karate+ (128K)

static struct BurnRomInfo SpecintkarateplusRomDesc[] = {
	{ "International Karate+ (1987)(System 3 Software).tap", 0x0d173, 0x097593e6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specintkarateplus, Specintkarateplus, Spec128)
STD_ROM_FN(Specintkarateplus)

struct BurnDriver BurnSpecintkarateplus = {
	"spec_intkarateplus", NULL, "spec_spec128", NULL, "1987",
	"International Karate+ (128K)\0", NULL, "System 3 Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecintkarateplusRomInfo, SpecintkarateplusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Robin of the Wood (128k)

static struct BurnRomInfo SpecrobinofwoodRomDesc[] = {
	{ "Robin of the Wood (1985)(Odin Computer Graphics)(128k).tap", 0x0f37e, 0xce069366, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrobinofwood, Specrobinofwood, Spec128)
STD_ROM_FN(Specrobinofwood)

struct BurnDriver BurnSpecrobinofwood = {
	"spec_robinofwood", NULL, "spec_spec128", NULL, "1985",
	"Robin of the Wood (128k)\0", NULL, "Odin Computer Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecrobinofwoodRomInfo, SpecrobinofwoodRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sidewize (128K)

static struct BurnRomInfo SpecsidewizeRomDesc[] = {
	{ "Sidewize (1987)(Firebird Software).tap", 0x0a2c8, 0xdb066855, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsidewize, Specsidewize, Spec128)
STD_ROM_FN(Specsidewize)

struct BurnDriver BurnSpecsidewize = {
	"spec_sidewize", NULL, "spec_spec128", NULL, "1987",
	"Sidewize (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsidewizeRomInfo, SpecsidewizeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Test Drive II - The Duel

static struct BurnRomInfo Spectestdrv2RomDesc[] = {
	{ "Test Drive II - The Duel (1989)(Accolade).z80", 0x0a8cd, 0x4047f7de, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectestdrv2, Spectestdrv2, Spec128)
STD_ROM_FN(Spectestdrv2)

struct BurnDriver BurnSpectestdrv2 = {
	"spec_testdrv2", NULL, "spec_spec128", NULL, "1989",
	"Test Drive II - The Duel\0", NULL, "Accolade", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectestdrv2RomInfo, Spectestdrv2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Plot, The

static struct BurnRomInfo SpecplottheRomDesc[] = {
	{ "Plot, The (1988)(Firebird Software).tap", 0x0af68, 0x1adf1093, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specplotthe, Specplotthe, Spec128)
STD_ROM_FN(Specplotthe)

struct BurnDriver BurnSpecplotthe = {
	"spec_plotthe", NULL, "spec_spec128", NULL, "1988",
	"Plot, The\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecplottheRomInfo, SpecplottheRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tintin on the Moon

static struct BurnRomInfo SpectintmoonRomDesc[] = {
	{ "Tintin on the Moon (1989)(Infogrames).tap", 0x09ca6, 0x1959b7a0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectintmoon, Spectintmoon, Spec128)
STD_ROM_FN(Spectintmoon)

struct BurnDriver BurnSpectintmoon = {
	"spec_tintmoon", NULL, "spec_spec128", NULL, "1989",
	"Tintin on the Moon\0", NULL, "Infogrames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectintmoonRomInfo, SpectintmoonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Turbo Cup Challenge

static struct BurnRomInfo SpecturbocupchRomDesc[] = {
	{ "Turbo Cup Challenge (1989)(Loriciels).tap", 0x0b19c, 0xb81e5707, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specturbocupch, Specturbocupch, Spec128)
STD_ROM_FN(Specturbocupch)

struct BurnDriver BurnSpecturbocupch = {
	"spec_turbocupch", NULL, "spec_spec128", NULL, "1989",
	"Turbo Cup Challenge\0", NULL, "Loriciels", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecturbocupchRomInfo, SpecturbocupchRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vigilante

static struct BurnRomInfo SpecvigilanteRomDesc[] = {
	{ "Vigilante (1989)(U.S. Gold).tap", 0x1b03a, 0x372bf21f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specvigilante, Specvigilante, Spec128)
STD_ROM_FN(Specvigilante)

struct BurnDriver BurnSpecvigilante = {
	"spec_vigilante", NULL, "spec_spec128", NULL, "1989",
	"Vigilante\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecvigilanteRomInfo, SpecvigilanteRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Impossamole

static struct BurnRomInfo SpecimpossamoleRomDesc[] = {
	{ "Impossamole (1990)(Gremlin Graphics Software).tap", 0x25438, 0xf1c505c7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specimpossamole, Specimpossamole, Spec128)
STD_ROM_FN(Specimpossamole)

struct BurnDriver BurnSpecimpossamole = {
	"spec_impossamole", NULL, "spec_spec128", NULL, "1990",
	"Impossamole\0", NULL, "The Edge Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecimpossamoleRomInfo, SpecimpossamoleRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// St. Dragon

static struct BurnRomInfo SpecstdragonRomDesc[] = {
	{ "St. Dragon (1990)(Storm Software).tap", 0x2e158, 0x6bb077c8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstdragon, Specstdragon, Spec128)
STD_ROM_FN(Specstdragon)

struct BurnDriver BurnSpecstdragon = {
	"spec_stdragon", NULL, "spec_spec128", NULL, "1990",
	"St. Dragon\0", NULL, "Storm Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecstdragonRomInfo, SpecstdragonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormlord II: Deliverance (Part 1 of 3)

static struct BurnRomInfo Specstormlord21RomDesc[] = {
	{ "Stormlord II Deliverance (1990)(Hewson Consultants)(Part 1 of 3).z80", 0x088fe, 0x1aae8085, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstormlord21, Specstormlord21, Spec128)
STD_ROM_FN(Specstormlord21)

struct BurnDriver BurnSpecstormlord21 = {
	"spec_stormlord21", NULL, "spec_spec128", NULL, "1990",
	"Stormlord II: Deliverance (Part 1 of 3)(128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specstormlord21RomInfo, Specstormlord21RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormlord II: Deliverance (Part 2 of 3)

static struct BurnRomInfo Specstormlord22RomDesc[] = {
	{ "Stormlord II Deliverance (1990)(Hewson Consultants)(Part 2 of 3).z80", 0x090b4, 0xfabc3ae4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstormlord22, Specstormlord22, Spec128)
STD_ROM_FN(Specstormlord22)

struct BurnDriver BurnSpecstormlord22 = {
	"spec_stormlord22", "spec_stormlord21", "spec_spec128", NULL, "1990",
	"Stormlord II: Deliverance (Part 2 of 3)(128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specstormlord22RomInfo, Specstormlord22RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormlord II: Deliverance (Part 3 of 3)

static struct BurnRomInfo Specstormlord23RomDesc[] = {
	{ "Stormlord II Deliverance (1990)(Hewson Consultants)(Part 3 of 3).z80", 0x08eb4, 0x115a02d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstormlord23, Specstormlord23, Spec128)
STD_ROM_FN(Specstormlord23)

struct BurnDriver BurnSpecstormlord23 = {
	"spec_stormlord23", "spec_stormlord21", "spec_spec128", NULL, "1990",
	"Stormlord II: Deliverance (Part 3 of 3)(128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specstormlord23RomInfo, Specstormlord23RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Desperado 2 (Spanish)

static struct BurnRomInfo Specdesperd2RomDesc[] = {
	{ "Desperado 2 (1991)(Topo Soft)(es).tap", 0x18bc6, 0xf1aca40a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdesperd2, Specdesperd2, Spec128)
STD_ROM_FN(Specdesperd2)

struct BurnDriver BurnSpecdesperd2 = {
	"spec_desperd2", NULL, "spec_spec128", NULL, "1991",
	"Desperado 2 (Spanish)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specdesperd2RomInfo, Specdesperd2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mad Mix 2 - En el Castillo de los Fantasmas (Spanish)

static struct BurnRomInfo SpecmamiencaRomDesc[] = {
	{ "Mad Mix 2 - En el Castillo de los Fantasmas (1990)(Topo Soft)(es).tap", 0x13ae2, 0x2e343a0c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmamienca, Specmamienca, Spec128)
STD_ROM_FN(Specmamienca)

struct BurnDriver BurnSpecmamienca = {
	"spec_mamienca", NULL, "spec_spec128", NULL, "1990",
	"Mad Mix 2 - En el Castillo de los Fantasmas (Spanish) (128K)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmamiencaRomInfo, SpecmamiencaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Paris-Dakar (Spanish)

static struct BurnRomInfo SpecparisdakaresRomDesc[] = {
	{ "Paris-Dakar (1988)(Zigurat Software)(es).tap", 0x0f960, 0x15728fdf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specparisdakares, Specparisdakares, Spec128)
STD_ROM_FN(Specparisdakares)

struct BurnDriver BurnSpecparisdakares = {
	"spec_parisdakares", NULL, "spec_spec128", NULL, "1988",
	"Paris-Dakar (Spanish)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecparisdakaresRomInfo, SpecparisdakaresRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Perico Delgado Maillot Amarillo (Spanish)

static struct BurnRomInfo SpecpericodelgadoRomDesc[] = {
	{ "Perico Delgado Maillot Amarillo (1989)(Topo Soft)(es).tap", 0x2497f, 0x284b616d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpericodelgado, Specpericodelgado, Spec128)
STD_ROM_FN(Specpericodelgado)

struct BurnDriver BurnSpecpericodelgado = {
	"spec_pericodelgado", NULL, "spec_spec128", NULL, "1989",
	"Perico Delgado Maillot Amarillo (Spanish)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpericodelgadoRomInfo, SpecpericodelgadoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Soviet (Part 1 of 2)

static struct BurnRomInfo Specsovietpart1RomDesc[] = {
	{ "Soviet (1990)(Opera Soft)(Part 1 of 2).tap", 0x15ec0, 0x407510c3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsovietpart1, Specsovietpart1, Spec128)
STD_ROM_FN(Specsovietpart1)

struct BurnDriver BurnSpecsovietpart1 = {
	"spec_sovietpart1", NULL, "spec_spec128", NULL, "1990",
	"Soviet (Part 1 of 2)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsovietpart1RomInfo, Specsovietpart1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Soviet (Part 2 of 2)

static struct BurnRomInfo Specsovietpart2RomDesc[] = {
	{ "Soviet (1990)(Opera Soft)(Part 2 of 2).tap", 0x186c4, 0x7db72ee5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsovietpart2, Specsovietpart2, Spec128)
STD_ROM_FN(Specsovietpart2)

struct BurnDriver BurnSpecsovietpart2 = {
	"spec_sovietpart2", "spec_sovietpart1", "spec_spec128", NULL, "1990",
	"Soviet (Part 2 of 2)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, Specsovietpart2RomInfo, Specsovietpart2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tour 91 (Spanish)

static struct BurnRomInfo Spectour91RomDesc[] = {
	{ "Tour 91 (1991)(Topo Soft)(es).tap", 0x26f73, 0xa27369fc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectour91, Spectour91, Spec128)
STD_ROM_FN(Spectour91)

struct BurnDriver BurnSpectour91 = {
	"spec_tour91", NULL, "spec_spec128", NULL, "1991",
	"Tour 91 (Spanish)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectour91RomInfo, Spectour91RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Viaje al Centro de la Tierra

static struct BurnRomInfo SpecviajecentterraRomDesc[] = {
	{ "Viaje al Centro de la Tierra (1989)(Topo Soft).tap", 0x1eaf1, 0xdbc5e51b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specviajecentterra, Specviajecentterra, Spec128)
STD_ROM_FN(Specviajecentterra)

struct BurnDriver BurnSpecviajecentterra = {
	"spec_viajecentterra", NULL, "spec_spec128", NULL, "1989",
	"Viaje al Centro de la Tierra\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecviajecentterraRomInfo, SpecviajecentterraRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// WWF WrestleMania (128K)

static struct BurnRomInfo SpecwwfwresmaniaRomDesc[] = {
	{ "WWF WrestleMania (1991)(Ocean Software).tap", 0x331b1, 0xb377350f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specwwfwresmania, Specwwfwresmania, Spec128)
STD_ROM_FN(Specwwfwresmania)

struct BurnDriver BurnSpecwwfwresmania = {
	"spec_wwfwresmania", NULL, "spec_spec128", NULL, "1991",
	"WWF WrestleMania (128K)\0", NULL, "Ocean Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecwwfwresmaniaRomInfo, SpecwwfwresmaniaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Zona 0 (Spanish)

static struct BurnRomInfo Speczona0RomDesc[] = {
	{ "Zona 0 (1991)(Topo Soft)(es).tap", 0x12b84, 0xdf7e9568, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speczona0, Speczona0, Spec128)
STD_ROM_FN(Speczona0)

struct BurnDriver BurnSpeczona0 = {
	"spec_zona0", NULL, "spec_spec128", NULL, "1991",
	"Zona 0 (Spanish)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Speczona0RomInfo, Speczona0RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Double Dragon

static struct BurnRomInfo Specddragon1RomDesc[] = {
	{ "Double Dragon (1989)(Melbourne House).tap", 0x29ffb, 0xa38e0c54, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specddragon1, Specddragon1, Spec128)
STD_ROM_FN(Specddragon1)

struct BurnDriver BurnSpecddragon1 = {
	"spec_ddragon1", NULL, "spec_spec128", NULL, "1989",
	"Double Dragon\0", NULL, "Melbourne House", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specddragon1RomInfo, Specddragon1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Double Dragon II: The Revenge

static struct BurnRomInfo Specddragon2RomDesc[] = {
	{ "Double Dragon II - The Revenge (1989)(Virgin Games).tap", 0x2d64f, 0xaf872e35, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specddragon2, Specddragon2, Spec128)
STD_ROM_FN(Specddragon2)

struct BurnDriver BurnSpecddragon2 = {
	"spec_ddragon2", NULL, "spec_spec128", NULL, "1989",
	"Double Dragon II: The Revenge\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specddragon2RomInfo, Specddragon2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mortadelo y Filemon II (Part 1 of 2) (Spanish)

static struct BurnRomInfo Specmortfilemon21RomDesc[] = {
	{ "Mortadelo y Filemon II (1990)(Dro Soft)(Part 1 of 2)(es).tap", 0x0bba9, 0x3735008e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmortfilemon21, Specmortfilemon21, Spec128)
STD_ROM_FN(Specmortfilemon21)

struct BurnDriver BurnSpecmortfilemon21 = {
	"spec_mortfilemon21", NULL, "spec_spec128", NULL, "1990",
	"Mortadelo y Filemon II (Part 1 of 2) (Spanish)\0", NULL, "Dro Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmortfilemon21RomInfo, Specmortfilemon21RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mortadelo y Filemon II (Part 2 of 2) (Spanish)

static struct BurnRomInfo Specmortfilemon22RomDesc[] = {
	{ "Mortadelo y Filemon II (1990)(Dro Soft)(Part 2 of 2)(es).tap", 0x0b6a3, 0x28472d0e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmortfilemon22, Specmortfilemon22, Spec128)
STD_ROM_FN(Specmortfilemon22)

struct BurnDriver BurnSpecmortfilemon22 = {
	"spec_mortfilemon22", NULL, "spec_spec128", NULL, "1990",
	"Mortadelo y Filemon II (Part 2 of 2) (Spanish)\0", NULL, "Dro Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specmortfilemon22RomInfo, Specmortfilemon22RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Desperado

static struct BurnRomInfo SpecDesperadoRomDesc[] = {
	{ "Desperado (1986)(Central Solutions).tap", 0x9a0a, 0x488f2cd5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDesperado, SpecDesperado, Spec128)
STD_ROM_FN(SpecDesperado)

struct BurnDriver BurnSpecDesperado = {
	"spec_desperado", NULL, "spec_spec128", NULL, "1986",
	"Desperado\0", NULL, "Central Solutions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDesperadoRomInfo, SpecDesperadoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mikie (48K)

static struct BurnRomInfo SpecMikieRomDesc[] = {
	{ "Mikie (1985)(Imagine Software).tap", 0xbc79, 0x45203b87, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMikie, SpecMikie, Spectrum)
STD_ROM_FN(SpecMikie)

struct BurnDriver BurnSpecMikie = {
	"spec_mikie", NULL, "spec_spectrum", NULL, "1985",
	"Mikie (48K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecMikieRomInfo, SpecMikieRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 	Super Trux

static struct BurnRomInfo SpecsupertruxRomDesc[] = {
	{ "Super Trux (1988)(Elite Systems).tap", 0xdd35, 0x65fcf302, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsupertrux, Specsupertrux, Spec128)
STD_ROM_FN(Specsupertrux)

struct BurnDriver BurnSpecsupertrux = {
	"spec_supertrux", NULL, "spec_spec128", NULL, "1988",
	"Super Trux\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsupertruxRomInfo, SpecsupertruxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Glider Rider

static struct BurnRomInfo SpecGliderrRomDesc[] = {
	{ "Glider Rider (1987)(Quicksilva).tap", 0xfe51, 0x2789b5bb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGliderr, SpecGliderr, Spec128)
STD_ROM_FN(SpecGliderr)

struct BurnDriver BurnSpecGliderr = {
	"spec_gliderr", NULL, "spec_spec128", NULL, "1987",
	"Glider Rider\0", NULL, "Quicksilva", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGliderrRomInfo, SpecGliderrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// North & South (128K)

static struct BurnRomInfo SpecnorthnsouthRomDesc[] = {
	{ "North & South (1991)(Infogrames)(128k).z80", 0x1bf6b, 0x8ca87f0d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnorthnsouth, Specnorthnsouth, Spec128)
STD_ROM_FN(Specnorthnsouth)

struct BurnDriver BurnSpecnorthnsouth = {
	"spec_northnsouth", NULL, "spec_spec128", NULL, "1991",
	"North & South (128K)\0", NULL, "Infogrames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnorthnsouthRomInfo, SpecnorthnsouthRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Afteroids (Spanish)

static struct BurnRomInfo SpecafteroidsRomDesc[] = {
	{ "Afteroids (1988)(Zigurat Software)(es).tap", 0x0c30b, 0x53f59e66, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specafteroids, Specafteroids, Spec128)
STD_ROM_FN(Specafteroids)

struct BurnDriver BurnSpecafteroids = {
	"spec_afteroids", NULL, "spec_spec128", NULL, "1988",
	"Afteroids (Spanish)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecafteroidsRomInfo, SpecafteroidsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Humphrey (Spanish)

static struct BurnRomInfo SpechumphreyRomDesc[] = {
	{ "Humphrey (1988)(Zigurat Software)(es).tap", 0x0c30b, 0xdd0455ba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechumphrey, Spechumphrey, Spec128)
STD_ROM_FN(Spechumphrey)

struct BurnDriver BurnSpechumphrey = {
	"spec_humphrey", NULL, "spec_spec128", NULL, "1988",
	"Humphrey (Spanish)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechumphreyRomInfo, SpechumphreyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shadow Dancer

static struct BurnRomInfo SpecshaddancerRomDesc[] = {
	{ "Shadow Dancer (1991)(U.S. Gold).tap", 0x239f7, 0xfaa90676, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specshaddancer, Specshaddancer, Spec128)
STD_ROM_FN(Specshaddancer)

struct BurnDriver BurnSpecshaddancer = {
	"spec_shaddancer", NULL, "spec_spec128", NULL, "1991",
	"Shadow Dancer\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecshaddancerRomInfo, SpecshaddancerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Airborne Ranger

static struct BurnRomInfo SpecairbornerangerRomDesc[] = {
	{ "Airborne Ranger (1988)(MicroProse Software).tap", 0x40e88, 0xa82ed5c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specairborneranger, Specairborneranger, Spec128)
STD_ROM_FN(Specairborneranger)

struct BurnDriver BurnSpecairborneranger = {
	"spec_airborneranger", NULL, "spec_spec128", NULL, "1988",
	"Airborne Ranger\0", NULL, "MicroProse Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecairbornerangerRomInfo, SpecairbornerangerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Project Stealth Fighter

static struct BurnRomInfo SpecprojstealthfighterRomDesc[] = {
	{ "Project Stealth Fighter (1990)(MicroProse Software).tap", 0x2bde9, 0xf0b7dc66, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specprojstealthfighter, Specprojstealthfighter, Spec128)
STD_ROM_FN(Specprojstealthfighter)

struct BurnDriver BurnSpecprojstealthfighter = {
	"spec_projstealthfighter", NULL, "spec_spec128", NULL, "1990",
	"Project Stealth Fighter\0", NULL, "MicroProse Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecprojstealthfighterRomInfo, SpecprojstealthfighterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Golden Axe

static struct BurnRomInfo Specgoldenaxe1RomDesc[] = {
	{ "Golden Axe (1990)(Virgin Games).z80", 0x1b859, 0x3f32f82f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgoldenaxe1, Specgoldenaxe1, Spec128)
STD_ROM_FN(Specgoldenaxe1)

struct BurnDriver BurnSpecgoldenaxe1 = {
	"spec_goldenaxe1", NULL, "spec_spec128", NULL, "1990",
	"Golden Axe\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgoldenaxe1RomInfo, Specgoldenaxe1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gunship

static struct BurnRomInfo SpecgunshipRomDesc[] = {
	{ "Gunship (1987)(MicroProse Software).tap", 0x193ca, 0x01e4ed88, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgunship, Specgunship, Spec128)
STD_ROM_FN(Specgunship)

struct BurnDriver BurnSpecgunship = {
	"spec_gunship", NULL, "spec_spec128", NULL, "1987",
	"Gunship\0", NULL, "MicroProse Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecgunshipRomInfo, SpecgunshipRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Desperado (Gunsmoke) (Trainer)

static struct BurnRomInfo Specdesperado1trnRomDesc[] = {
	{ "Desperado (1987)(Topo Soft)(Trainer).tap", 149093, 0xff12d868, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdesperado1trn, Specdesperado1trn, Spec128)
STD_ROM_FN(Specdesperado1trn)

struct BurnDriver BurnSpecdesperado1trn = {
	"spec_desperado1trn", NULL, "spec_spec128", NULL, "1987",
	"Desperado (Gunsmoke) (Trainer)\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specdesperado1trnRomInfo, Specdesperado1trnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Wars (48K)

static struct BurnRomInfo SpecStarwarsRomDesc[] = {
	{ "Star Wars.tap", 0xa323, 0x14f2595f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarwars, SpecStarwars, Spectrum)
STD_ROM_FN(SpecStarwars)

struct BurnDriver BurnSpecStarwars = {
	"spec_starwars", NULL, "spec_spectrum", NULL, "1987",
	"Star Wars (48K)\0", NULL, "Domark Ltd (UK)", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecStarwarsRomInfo, SpecStarwarsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Action Force (48K)

static struct BurnRomInfo SpecActionforceRomDesc[] = {
	{ "Action Force (1987)(Virgin Games).tap", 47303, 0x3ed87428, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecActionforce, SpecActionforce, Spectrum)
STD_ROM_FN(SpecActionforce)

struct BurnDriver BurnSpecActionforce = {
	"spec_actionforce", NULL, "spec_spectrum", NULL, "1987",
	"Action Force (48K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecActionforceRomInfo, SpecActionforceRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Action Force II (128K)

static struct BurnRomInfo Specactionforce2RomDesc[] = {
	{ "Action Force II (1988)(Virgin Games).tap", 46813, 0x3d4139b7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specactionforce2, Specactionforce2, Spec128)
STD_ROM_FN(Specactionforce2)

struct BurnDriver BurnSpecactionforce2 = {
	"spec_actionforce2", NULL, "spec_spec128", NULL, "1988",
	"Action Force II (128K)\0", "On black screen press SPACE to begin", "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, Specactionforce2RomInfo, Specactionforce2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Action Reflex

static struct BurnRomInfo SpecactionreflexRomDesc[] = {
	{ "Action Reflex (1986)(Mirrorsoft).tap", 41597, 0xaada6209, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specactionreflex, Specactionreflex, Spec128)
STD_ROM_FN(Specactionreflex)

struct BurnDriver BurnSpecactionreflex = {
	"spec_actionreflex", NULL, "spec_spec128", NULL, "1986",
	"Action Reflex\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecactionreflexRomInfo, SpecactionreflexRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hostages

static struct BurnRomInfo SpechostagesRomDesc[] = {
	{ "Hostages (1990)(Infogrames).tap", 222554, 0x171b72b5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechostages, Spechostages, Spec128)
STD_ROM_FN(Spechostages)

struct BurnDriver BurnSpechostages = {
	"spec_hostages", NULL, "spec_spec128", NULL, "1990",
	"Hostages\0", NULL, "Infogrames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechostagesRomInfo, SpechostagesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ranarama (48K)

static struct BurnRomInfo SpecranaramaRomDesc[] = {
	{ "Ranarama (1987)(Hewson Consultants).z80", 38541, 0x4894e422, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specranarama, Specranarama, Spectrum)
STD_ROM_FN(Specranarama)

struct BurnDriver BurnSpecranarama = {
	"spec_ranarama", NULL, "spec_spectrum", NULL, "1987",
	"Ranarama (48K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MAZE | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecranaramaRomInfo, SpecranaramaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Zynaps

static struct BurnRomInfo SpeczynapsRomDesc[] = {
	{ "Zynaps (1987)(Hewson Consultants).tap", 42473, 0x2e272efc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speczynaps, Speczynaps, Spec128)
STD_ROM_FN(Speczynaps)

struct BurnDriver BurnSpeczynaps = {
	"spec_zynaps", NULL, "spec_spec128", NULL, "1987",
	"Zynaps\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpeczynapsRomInfo, SpeczynapsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Uridium

static struct BurnRomInfo SpecuridiumRomDesc[] = {
	{ "Uridium (1986)(Hewson Consultants).tap", 27747, 0x28bdbbfc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specuridium, Specuridium, Spec128)
STD_ROM_FN(Specuridium)

struct BurnDriver BurnSpecuridium = {
	"spec_uridium", NULL, "spec_spec128", NULL, "1986",
	"Uridium\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecuridiumRomInfo, SpecuridiumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Netherworld (128K)

static struct BurnRomInfo SpecnetherworldRomDesc[] = {
	{ "Netherworld (1988)(Hewson Consultants).tap", 44343, 0x48db7496, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnetherworld, Specnetherworld, Spec128)
STD_ROM_FN(Specnetherworld)

struct BurnDriver BurnSpecnetherworld = {
	"spec_netherworld", NULL, "spec_spec128", NULL, "1988",
	"Netherworld (128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnetherworldRomInfo, SpecnetherworldRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Marauder (128K)

static struct BurnRomInfo SpecmarauderRomDesc[] = {
	{ "Marauder (1988)(Hewson Consultants).tap", 54339, 0xcbd7f723, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmarauder, Specmarauder, Spec128)
STD_ROM_FN(Specmarauder)

struct BurnDriver BurnSpecmarauder = {
	"spec_marauder", NULL, "spec_spec128", NULL, "1988",
	"Marauder (128K)\0", NULL, "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecmarauderRomInfo, SpecmarauderRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kraal (48K)

static struct BurnRomInfo SpeckraalRomDesc[] = {
	{ "Kraal (1990)(Hewson Consultants).tap", 57614, 0x84276314, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckraal, Speckraal, Spectrum)
STD_ROM_FN(Speckraal)

struct BurnDriver BurnSpeckraal = {
	"spec_kraal", NULL, "spec_spectrum", NULL, "1990",
	"Kraal (48K)\0", "Use Keyboard", "Hewson Consultants", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeckraalRomInfo, SpeckraalRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sentinel, The

static struct BurnRomInfo SpecthesentinelRomDesc[] = {
	{ "Sentinel, The (1987)(Firebird Software).z80", 39141, 0x716e7ebf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specthesentinel, Specthesentinel, Spec128)
STD_ROM_FN(Specthesentinel)

struct BurnDriver BurnSpecthesentinel = {
	"spec_thesentinel", NULL, "spec_spec128", NULL, "1987",
	"Sentinel, The\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecthesentinelRomInfo, SpecthesentinelRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Indiana Jones and the Last Crusade

static struct BurnRomInfo SpecindylastcrusadeRomDesc[] = {
	{ "Indiana Jones and the Last Crusade (1989)(U.S. Gold).tap", 145470, 0xe43fcead, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specindylastcrusade, Specindylastcrusade, Spec128)
STD_ROM_FN(Specindylastcrusade)

struct BurnDriver BurnSpecindylastcrusade = {
	"spec_indylastcrusade", NULL, "spec_spec128", NULL, "1989",
	"Indiana Jones and the Last Crusade\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecindylastcrusadeRomInfo, SpecindylastcrusadeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Indiana Jones and the Temple of Doom

static struct BurnRomInfo SpecindytempledoomRomDesc[] = {
	{ "Indiana Jones and the Temple of Doom (1987)(U.S. Gold).tap", 76529, 0x2c9fa63e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specindytempledoom, Specindytempledoom, Spec128)
STD_ROM_FN(Specindytempledoom)

struct BurnDriver BurnSpecindytempledoom = {
	"spec_indytempledoom", NULL, "spec_spec128", NULL, "1987",
	"Indiana Jones and the Temple of Doom\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecindytempledoomRomInfo, SpecindytempledoomRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spindizzy (48K)

static struct BurnRomInfo SpecspindizzyRomDesc[] = {
	{ "Spindizzy (1986)(Electric Dreams).tap", 47877, 0x568a095a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specspindizzy, Specspindizzy, Spectrum)
STD_ROM_FN(Specspindizzy)

struct BurnDriver BurnSpecspindizzy = {
	"spec_spindizzy", NULL, "spec_spectrum", NULL, "1986",
	"Spindizzy (48K)\0", NULL, "Electric Dreams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecspindizzyRomInfo, SpecspindizzyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hopping Mad

static struct BurnRomInfo SpechoppingmadRomDesc[] = {
	{ "Hopping Mad (1988)(Elite Systems).tap", 40347, 0x620ba84c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechoppingmad, Spechoppingmad, Spec128)
STD_ROM_FN(Spechoppingmad)

struct BurnDriver BurnSpechoppingmad = {
	"spec_hoppingmad", NULL, "spec_spec128", NULL, "1988",
	"Hopping Mad\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechoppingmadRomInfo, SpechoppingmadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Indiana Jones and the Fate of Atlantis

static struct BurnRomInfo SpecindyfateatlantisRomDesc[] = {
	{ "Indiana Jones and the Fate of Atlantis (1992)(U.S. Gold).tap", 366350, 0xb6341899, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specindyfateatlantis, Specindyfateatlantis, Spec128)
STD_ROM_FN(Specindyfateatlantis)

struct BurnDriver BurnSpecindyfateatlantis = {
	"spec_indyfateatlantis", NULL, "spec_spec128", NULL, "1992",
	"Indiana Jones and the Fate of Atlantis\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecindyfateatlantisRomInfo, SpecindyfateatlantisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Black Lamp (128K)

static struct BurnRomInfo SpecblacklampRomDesc[] = {
	{ "Black Lamp (1988)(Firebird Software).tap", 47656, 0xaffec634, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specblacklamp, Specblacklamp, Spec128)
STD_ROM_FN(Specblacklamp)

struct BurnDriver BurnSpecblacklamp = {
	"spec_blacklamp", NULL, "spec_spec128", NULL, "1988",
	"Black Lamp (128K)\0", NULL, "Firebird Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecblacklampRomInfo, SpecblacklampRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dark Fusion

static struct BurnRomInfo SpecdarkfusionRomDesc[] = {
	{ "Dark Fusion (1988)(Gremlin Graphics).tap", 65624, 0x87c7d4d6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdarkfusion, Specdarkfusion, Spec128)
STD_ROM_FN(Specdarkfusion)

struct BurnDriver BurnSpecdarkfusion = {
	"spec_darkfusion", NULL, "spec_spec128", NULL, "1988",
	"Dark Fusion\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdarkfusionRomInfo, SpecdarkfusionRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Switchblade

static struct BurnRomInfo SpecswitchbladeRomDesc[] = {
	{ "Switchblade (1991)(Gremlin Graphics).tap", 52937, 0xa050f9d6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specswitchblade, Specswitchblade, Spec128)
STD_ROM_FN(Specswitchblade)

struct BurnDriver BurnSpecswitchblade = {
	"spec_switchblade", NULL, "spec_spec128", NULL, "1991",
	"Switchblade\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecswitchbladeRomInfo, SpecswitchbladeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// E-SWAT

static struct BurnRomInfo SpeceswatRomDesc[] = {
	{ "E-SWAT (1990)(U.S. Gold).tap", 213337, 0x721ab30f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speceswat, Speceswat, Spec128)
STD_ROM_FN(Speceswat)

struct BurnDriver BurnSpeceswat = {
	"spec_eswat", NULL, "spec_spec128", NULL, "1990",
	"E-SWAT\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeceswatRomInfo, SpeceswatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Final Fight

static struct BurnRomInfo SpecfinalfightRomDesc[] = {
	{ "Final Fight (1991)(U.S. Gold).tap", 199327, 0x8993d2c6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfinalfight, Specfinalfight, Spec128)
STD_ROM_FN(Specfinalfight)

struct BurnDriver BurnSpecfinalfight = {
	"spec_finalfight", NULL, "spec_spec128", NULL, "1991",
	"Final Fight\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfinalfightRomInfo, SpecfinalfightRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ikari Warriors

static struct BurnRomInfo SpecikariwarriorsRomDesc[] = {
	{ "Ikari Warriors (1988)(Elite Systems).tap", 47889, 0xf3fac369, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specikariwarriors, Specikariwarriors, Spec128)
STD_ROM_FN(Specikariwarriors)

struct BurnDriver BurnSpecikariwarriors = {
	"spec_ikariwarriors", NULL, "spec_spec128", NULL, "1988",
	"Ikari Warriors\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecikariwarriorsRomInfo, SpecikariwarriorsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mercs

static struct BurnRomInfo SpecmercsRomDesc[] = {
	{ "Mercs (1991)(U.S. Gold).tap", 191310, 0x8b4f3dc9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmercs, Specmercs, Spec128)
STD_ROM_FN(Specmercs)

struct BurnDriver BurnSpecmercs = {
	"spec_mercs", NULL, "spec_spec128", NULL, "1991",
	"Mercs\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecmercsRomInfo, SpecmercsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Turrican (48K)

static struct BurnRomInfo Specturrican1RomDesc[] = {
	{ "Turrican (1990)(Rainbow Arts).tap", 266448, 0xc5cfad1a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specturrican1, Specturrican1, Spectrum)
STD_ROM_FN(Specturrican1)

struct BurnDriver BurnSpecturrican1 = {
	"spec_turrican1", NULL, "spec_spectrum", NULL, "1990",
	"Turrican (48K)\0", NULL, "Rainbow Arts", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, Specturrican1RomInfo, Specturrican1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Turrican II (128K)

static struct BurnRomInfo Specturrican2RomDesc[] = {
	{ "Turrican II (1991)(Rainbow Arts).tap", 357318, 0x48f546ce, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specturrican2, Specturrican2, Spec128)
STD_ROM_FN(Specturrican2)

struct BurnDriver BurnSpecturrican2 = {
	"spec_turrican2", NULL, "spec_spec128", NULL, "1991",
	"Turrican II (128K)\0", NULL, "Rainbow Arts", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, Specturrican2RomInfo, Specturrican2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tetris 2

static struct BurnRomInfo Spectetris2RomDesc[] = {
	{ "Tetris 2 (1990)(Fuxoft).tap", 24510, 0xeb9220fd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectetris2, Spectetris2, Spec128)
STD_ROM_FN(Spectetris2)

struct BurnDriver BurnSpectetris2 = {
	"spec_tetris2", NULL, "spec_spec128", NULL, "1990",
	"Tetris 2\0", NULL, "Fuxoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spectetris2RomInfo, Spectetris2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 007 - Licence to Kill (128K)

static struct BurnRomInfo SpeclicencetokillRomDesc[] = {
	{ "Licence to Kill (1989)(Domark).tap", 48133, 0x44505823, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclicencetokill, Speclicencetokill, Spec128)
STD_ROM_FN(Speclicencetokill)

struct BurnDriver BurnSpeclicencetokill = {
	"spec_licencetokill", NULL, "spec_spec128", NULL, "1989",
	"007 - Licence to Kill (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpeclicencetokillRomInfo, SpeclicencetokillRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Finders Keepers

static struct BurnRomInfo SpecfinderskeepersRomDesc[] = {
	{ "Finders Keepers (1985)(Mastertronic).tap", 44174, 0x06f43742, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfinderskeepers, Specfinderskeepers, Spec128)
STD_ROM_FN(Specfinderskeepers)

struct BurnDriver BurnSpecfinderskeepers = {
	"spec_finderskeepers", NULL, "spec_spec128", NULL, "1985",
	"Finders Keepers\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfinderskeepersRomInfo, SpecfinderskeepersRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spellbound

static struct BurnRomInfo SpecspellboundRomDesc[] = {
	{ "Spellbound (1985)(Mastertronic).tap", 112432, 0x275e1ec1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specspellbound, Specspellbound, Spec128)
STD_ROM_FN(Specspellbound)

struct BurnDriver BurnSpecspellbound = {
	"spec_spellbound", NULL, "spec_spec128", NULL, "1985",
	"Spellbound\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecspellboundRomInfo, SpecspellboundRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Knight Tyme

static struct BurnRomInfo SpecknighttymeRomDesc[] = {
	{ "Knight Tyme (1986)(Mastertronic).tap", 119348, 0x79c80fac, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specknighttyme, Specknighttyme, Spec128)
STD_ROM_FN(Specknighttyme)

struct BurnDriver BurnSpecknighttyme = {
	"spec_knighttyme", NULL, "spec_spec128", NULL, "1986",
	"Knight Tyme\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecknighttymeRomInfo, SpecknighttymeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormbringer (128K)

static struct BurnRomInfo SpecstormbringerRomDesc[] = {
	{ "Stormbringer (1987)(Mastertronic).tap", 102898, 0x8819d367, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstormbringer, Specstormbringer, Spec128)
STD_ROM_FN(Specstormbringer)

struct BurnDriver BurnSpecstormbringer = {
	"spec_stormbringer", NULL, "spec_spec128", NULL, "1987",
	"Stormbringer (128K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecstormbringerRomInfo, SpecstormbringerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hades Nebula

static struct BurnRomInfo SpechadesnebulaRomDesc[] = {
	{ "Hades Nebula (1987)(Nexus Productions).tap", 43264, 0xbeac467c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechadesnebula, Spechadesnebula, Spectrum)
STD_ROM_FN(Spechadesnebula)

struct BurnDriver BurnSpechadesnebula = {
	"spec_hadesnebula", NULL, "spec_spectrum", NULL, "1987",
	"Hades Nebula\0", NULL, "Nexus Productions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechadesnebulaRomInfo, SpechadesnebulaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Arc of Yesod, The

static struct BurnRomInfo SpecarcofyesodRomDesc[] = {
	{ "Arc of Yesod, The (1985)(Thor Computer Software).tap", 74016, 0xb4c5351b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specarcofyesod, Specarcofyesod, Spec128)
STD_ROM_FN(Specarcofyesod)

struct BurnDriver BurnSpecarcofyesod = {
	"spec_arcofyesod", NULL, "spec_spec128", NULL, "1985",
	"Arc of Yesod, The\0", NULL, "Thor Computer Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecarcofyesodRomInfo, SpecarcofyesodRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nodes of Yesod

static struct BurnRomInfo SpecnodesofyesodRomDesc[] = {
	{ "Nodes of Yesod (1985)(Odin Computer Graphics).tap", 76905, 0x1c251259, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnodesofyesod, Specnodesofyesod, Spec128)
STD_ROM_FN(Specnodesofyesod)

struct BurnDriver BurnSpecnodesofyesod = {
	"spec_nodesofyesod", NULL, "spec_spec128", NULL, "1985",
	"Nodes of Yesod\0", NULL, "Odin Computer Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnodesofyesodRomInfo, SpecnodesofyesodRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blood Brothers (Module 1)

static struct BurnRomInfo Specbloodbrothers1RomDesc[] = {
	{ "Blood Brothers (Module 1)(1988)(Gremlin Graphics).z80", 42931, 0xb32fcea7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbloodbrothers1, Specbloodbrothers1, Spec128)
STD_ROM_FN(Specbloodbrothers1)

struct BurnDriver BurnSpecbloodbrothers1 = {
	"spec_bloodbrothers1", NULL, "spec_spec128", NULL, "1988",
	"Blood Brothers (Module 1)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbloodbrothers1RomInfo, Specbloodbrothers1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blood Brothers (Module 2)

static struct BurnRomInfo Specbloodbrothers2RomDesc[] = {
	{ "Blood Brothers (Module 2)(1988)(Gremlin Graphics).z80", 42790, 0x3fc07585, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbloodbrothers2, Specbloodbrothers2, Spec128)
STD_ROM_FN(Specbloodbrothers2)

struct BurnDriver BurnSpecbloodbrothers2 = {
	"spec_bloodbrothers2", "spec_bloodbrothers1", "spec_spec128", NULL, "1988",
	"Blood Brothers (Module 2)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbloodbrothers2RomInfo, Specbloodbrothers2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blood Brothers (Module 3)

static struct BurnRomInfo Specbloodbrothers3RomDesc[] = {
	{ "Blood Brothers (Module 3)(1988)(Gremlin Graphics).z80", 42935, 0x735098ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbloodbrothers3, Specbloodbrothers3, Spec128)
STD_ROM_FN(Specbloodbrothers3)

struct BurnDriver BurnSpecbloodbrothers3 = {
	"spec_bloodbrothers3", "spec_bloodbrothers1", "spec_spec128", NULL, "1988",
	"Blood Brothers (Module 3)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specbloodbrothers3RomInfo, Specbloodbrothers3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Destiny Mission

static struct BurnRomInfo SpecdestinymissionRomDesc[] = {
	{ "Destiny Mission (1990)(Williams Technology).tap", 42759, 0x71528fa3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdestinymission, Specdestinymission, Spec128)
STD_ROM_FN(Specdestinymission)

struct BurnDriver BurnSpecdestinymission = {
	"spec_destinymission", NULL, "spec_spec128", NULL, "1990",
	"Destiny Mission\0", NULL, "Williams Technology", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdestinymissionRomInfo, SpecdestinymissionRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Frost Byte

static struct BurnRomInfo SpecfrostbyteRomDesc[] = {
	{ "Frost Byte (1986)(Mikro-Gen).tap", 49465, 0x895fc6d8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfrostbyte, Specfrostbyte, Spec128)
STD_ROM_FN(Specfrostbyte)

struct BurnDriver BurnSpecfrostbyte = {
	"spec_frostbyte", NULL, "spec_spec128", NULL, "1986",
	"Frost Byte\0", NULL, "Mikro-Gen", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecfrostbyteRomInfo, SpecfrostbyteRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gauntlet III - The Final Quest

static struct BurnRomInfo Specgauntlet3RomDesc[] = {
	{ "Gauntlet III - The Final Quest (1991)(U.S. Gold).tap", 344602, 0x8c5a72ce, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgauntlet3, Specgauntlet3, Spec128)
STD_ROM_FN(Specgauntlet3)

struct BurnDriver BurnSpecgauntlet3 = {
	"spec_gauntlet3", NULL, "spec_spec128", NULL, "1991",
	"Gauntlet III - The Final Quest\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specgauntlet3RomInfo, Specgauntlet3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hydrofool

static struct BurnRomInfo SpechydrofoolRomDesc[] = {
	{ "Hydrofool (1987)(Faster Than Light).tap", 45889, 0x82e3db69, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechydrofool, Spechydrofool, Spec128)
STD_ROM_FN(Spechydrofool)

struct BurnDriver BurnSpechydrofool = {
	"spec_hydrofool", NULL, "spec_spec128", NULL, "1987",
	"Hydrofool\0", NULL, "Faster Than Light", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpechydrofoolRomInfo, SpechydrofoolRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kong's Revenge (Part 1)

static struct BurnRomInfo Speckongsrevenge1RomDesc[] = {
	{ "Kong's Revenge (Part 1)(1991)(Zigurat Software).tap", 56868, 0x20a7e7ce, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckongsrevenge1, Speckongsrevenge1, Spec128)
STD_ROM_FN(Speckongsrevenge1)

struct BurnDriver BurnSpeckongsrevenge1 = {
	"spec_kongsrevenge1", NULL, "spec_spec128", NULL, "1991",
	"Kong's Revenge (Part 1)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PLATFORM, 0,
	SpectrumGetZipName, Speckongsrevenge1RomInfo, Speckongsrevenge1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kong's Revenge (Part 2)

static struct BurnRomInfo Speckongsrevenge2RomDesc[] = {
	{ "Kong's Revenge (Part 2)(1991)(Zigurat Software).tap", 56868, 0xdf77b41f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckongsrevenge2, Speckongsrevenge2, Spec128)
STD_ROM_FN(Speckongsrevenge2)

struct BurnDriver BurnSpeckongsrevenge2 = {
	"spec_kongsrevenge2", "spec_kongsrevenge1", "spec_spec128", NULL, "1991",
	"Kong's Revenge (Part 2)\0", NULL, "Zigurat Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PLATFORM, 0,
	SpectrumGetZipName, Speckongsrevenge2RomInfo, Speckongsrevenge2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// North Star

static struct BurnRomInfo SpecnorthstarRomDesc[] = {
	{ "North Star (1988)(Gremlin Graphics).tap", 49369, 0x61d487db, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnorthstar, Specnorthstar, Spec128)
STD_ROM_FN(Specnorthstar)

struct BurnDriver BurnSpecnorthstar = {
	"spec_northstar", NULL, "spec_spec128", NULL, "1988",
	"North Star\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnorthstarRomInfo, SpecnorthstarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Athena (128K)

static struct BurnRomInfo SpecathenaRomDesc[] = {
	{ "Athena (1987)(Imagine Software).z80", 108395, 0xf0547094, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specathena, Specathena, Spec128)
STD_ROM_FN(Specathena)

struct BurnDriver BurnSpecathena = {
	"spec_athena", NULL, "spec_spec128", NULL, "1987",
	"Athena (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecathenaRomInfo, SpecathenaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Psycho Hopper

static struct BurnRomInfo SpecpsychohopperRomDesc[] = {
	{ "Psycho Hopper (1990)(Mastertronic Plus).tap", 50178, 0xbeda1169, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpsychohopper, Specpsychohopper, Spectrum)
STD_ROM_FN(Specpsychohopper)

struct BurnDriver BurnSpecpsychohopper = {
	"spec_psychohopper", NULL, "spec_spectrum", NULL, "1990",
	"Psycho Hopper\0", NULL, "Mastertronic Plus", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpsychohopperRomInfo, SpecpsychohopperRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Psycho Pigs U.X.B.

static struct BurnRomInfo SpecpsychopigsRomDesc[] = {
	{ "Psycho Pigs U.X.B. (1988)(U.S. Gold).tap", 50190, 0x3632f18a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpsychopigs, Specpsychopigs, Spec128)
STD_ROM_FN(Specpsychopigs)

struct BurnDriver BurnSpecpsychopigs = {
	"spec_psychopigs", NULL, "spec_spec128", NULL, "1988",
	"Psycho Pigs U.X.B.\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecpsychopigsRomInfo, SpecpsychopigsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Snoopy

static struct BurnRomInfo SpecsnoopyRomDesc[] = {
	{ "Snoopy (1990)(The Edge).tap", 40149, 0xa24e7539, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsnoopy, Specsnoopy, Spectrum)
STD_ROM_FN(Specsnoopy)

struct BurnDriver BurnSpecsnoopy = {
	"spec_snoopy", NULL, "spec_spectrum", NULL, "1990",
	"Snoopy\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecsnoopyRomInfo, SpecsnoopyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIssue2DIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stainless Steel

static struct BurnRomInfo SpecstainlesssteelRomDesc[] = {
	{ "Stainless Steel (1986)(Mikro-Gen).tap", 44663, 0xc01c4113, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specstainlesssteel, Specstainlesssteel, Spec128)
STD_ROM_FN(Specstainlesssteel)

struct BurnDriver BurnSpecstainlesssteel = {
	"spec_stainlesssteel", NULL, "spec_spec128", NULL, "1986",
	"Stainless Steel\0", NULL, "Mikro-Gen", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecstainlesssteelRomInfo, SpecstainlesssteelRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tai Chi Tortoise

static struct BurnRomInfo SpectaichitortoiseRomDesc[] = {
	{ "Tai Chi Tortoise (1991)(Zeppelin Games).tap", 29553, 0x5a60396a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectaichitortoise, Spectaichitortoise, Spec128)
STD_ROM_FN(Spectaichitortoise)

struct BurnDriver BurnSpectaichitortoise = {
	"spec_taichitortoise", NULL, "spec_spec128", NULL, "1991",
	"Tai Chi Tortoise\0", NULL, "Zeppelin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpectaichitortoiseRomInfo, SpectaichitortoiseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Towdie (English)

static struct BurnRomInfo SpectowdieenRomDesc[] = {
	{ "Towdie (English)(1994)(Ultrasoft).tap", 55179, 0x20bd2e70, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectowdieen, Spectowdieen, Spec128)
STD_ROM_FN(Spectowdieen)

struct BurnDriver BurnSpectowdieen = {
	"spec_towdieen", NULL, "spec_spec128", NULL, "1994",
	"Towdie (English)\0", NULL, "Ultrasoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpectowdieenRomInfo, SpectowdieenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Typhoon (128K)

static struct BurnRomInfo SpectyphoonRomDesc[] = {
	{ "Typhoon (1988)(Imagine Software).tap", 130475, 0x1ab3aa4a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectyphoon, Spectyphoon, Spec128)
STD_ROM_FN(Spectyphoon)

struct BurnDriver BurnSpectyphoon = {
	"spec_typhoon", NULL, "spec_spec128", NULL, "1988",
	"Typhoon (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpectyphoonRomInfo, SpectyphoonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Silent Shadow

static struct BurnRomInfo SpecsilentshadowRomDesc[] = {
	{ "Silent Shadow (1988)(Topo Soft).tap", 146162, 0xa6fb7413, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsilentshadow, Specsilentshadow, Spec128)
STD_ROM_FN(Specsilentshadow)

struct BurnDriver BurnSpecsilentshadow = {
	"spec_silentshadow", NULL, "spec_spec128", NULL, "1988",
	"Silent Shadow\0", NULL, "Topo Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecsilentshadowRomInfo, SpecsilentshadowRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Night Breed

static struct BurnRomInfo SpecnbreedRomDesc[] = {
	{ "Night Breed (1990)(Ocean).tap", 188653, 0x5701dcaf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnbreed, Specnbreed, Spec128)
STD_ROM_FN(Specnbreed)

struct BurnDriver BurnSpecnbreed = {
	"spec_nbreed", NULL, "spec_spec128", NULL, "1990",
	"Night Breed\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecnbreedRomInfo, SpecnbreedRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rescate en el Golfo (Part 1 of 2)

static struct BurnRomInfo Specrescategolfo1RomDesc[] = {
	{ "Rescate en el Golfo (1990)(Opera Soft)(Part 1 of 2).z80", 34779, 0xd7add860, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrescategolfo1, Specrescategolfo1, Spec128)
STD_ROM_FN(Specrescategolfo1)

struct BurnDriver BurnSpecrescategolfo1 = {
	"spec_rescategolfo1", NULL, "spec_spec128", NULL, "1990",
	"Rescate en el Golfo (Part 1 of 2)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrescategolfo1RomInfo, Specrescategolfo1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rescate en el Golfo (Part 2 of 2)

static struct BurnRomInfo Specrescategolfo2RomDesc[] = {
	{ "Rescate en el Golfo (1990)(Opera Soft)(Part 2 of 2).z80", 37620, 0x370d2085, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrescategolfo2, Specrescategolfo2, Spec128)
STD_ROM_FN(Specrescategolfo2)

struct BurnDriver BurnSpecrescategolfo2 = {
	"spec_rescategolfo2", NULL, "spec_spec128", NULL, "1990",
	"Rescate en el Golfo (Part 2 of 2)\0", NULL, "Opera Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Specrescategolfo2RomInfo, Specrescategolfo2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bobby Bearing

static struct BurnRomInfo SpecbobbybearingRomDesc[] = {
	{ "Bobby Bearing (1986)(The Edge).tap", 27778, 0x6b6895d2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbobbybearing, Specbobbybearing, Spec128)
STD_ROM_FN(Specbobbybearing)

struct BurnDriver BurnSpecbobbybearing = {
	"spec_bobbybearing", NULL, "spec_spec128", NULL, "1986",
	"Bobby Bearing\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecbobbybearingRomInfo, SpecbobbybearingRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Incredible Shrinking Sphere

static struct BurnRomInfo SpecincshrinksphereRomDesc[] = {
	{ "Incredible Shrinking Sphere (1989)(Electric Dreams).tap", 42625, 0xf89b035d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specincshrinksphere, Specincshrinksphere, Spec128)
STD_ROM_FN(Specincshrinksphere)

struct BurnDriver BurnSpecincshrinksphere = {
	"spec_incshrinksphere", NULL, "spec_spec128", NULL, "1989",
	"Incredible Shrinking Sphere\0", NULL, "Electric Dreams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecincshrinksphereRomInfo, SpecincshrinksphereRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alien Storm (128K)

static struct BurnRomInfo SpecAlienstormRomDesc[] = {
	{ "Alien Storm (1991)(U.S. Gold).tap", 252760, 0x8aa6b74d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAlienstorm, SpecAlienstorm, Spec128)
STD_ROM_FN(SpecAlienstorm)

struct BurnDriver BurnSpecAlienstorm = {
	"spec_alienstorm", NULL, "spec_spec128", NULL, "1991",
	"Alien Storm (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_SCRFIGHT | GBF_SHOOT, 0,
	SpectrumGetZipName, SpecAlienstormRomInfo, SpecAlienstormRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Guerrilla War (128K)

static struct BurnRomInfo SpecGuerrillawarRomDesc[] = {
	{ "Guerrilla War (1988)(Imagine Software).z80", 91016, 0xaf3aabbe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGuerrillawar, SpecGuerrillawar, Spec128)
STD_ROM_FN(SpecGuerrillawar)

struct BurnDriver BurnSpecGuerrillawar = {
	"spec_guerrillawar", NULL, "spec_spec128", NULL, "1988",
	"Guerrilla War (128K)\0", NULL, "Imagine Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecGuerrillawarRomInfo, SpecGuerrillawarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Predator

static struct BurnRomInfo SpecPredatorRomDesc[] = {
	{ "Predator (1987)(Activision).tap", 147030, 0xe6ffa821, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPredator, SpecPredator, Spec128)
STD_ROM_FN(SpecPredator)

struct BurnDriver BurnSpecPredator = {
	"spec_predator", NULL, "spec_spec128", NULL, "1987",
	"Predator\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPredatorRomInfo, SpecPredatorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Predator 2

static struct BurnRomInfo SpecPredator2RomDesc[] = {
	{ "Predator 2 (1991)(Image Works).tap", 120822, 0xa287648e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPredator2, SpecPredator2, Spec128)
STD_ROM_FN(SpecPredator2)

struct BurnDriver BurnSpecPredator2 = {
	"spec_predator2", NULL, "spec_spec128", NULL, "1991",
	"Predator 2\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPredator2RomInfo, SpecPredator2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Street Fighter II

static struct BurnRomInfo SpecStreetfighter2RomDesc[] = {
	{ "Street Fighter II (1993)(Go).tap", 519072, 0x052f9f1f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStreetfighter2, SpecStreetfighter2, Spec128)
STD_ROM_FN(SpecStreetfighter2)

struct BurnDriver BurnSpecStreetfighter2 = {
	"spec_streetfighter2", NULL, "spec_spec128", NULL, "1993",
	"Street Fighter II\0", NULL, "Go", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStreetfighter2RomInfo, SpecStreetfighter2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Terminator 2 - Judgement Day

static struct BurnRomInfo SpecTerminator2RomDesc[] = {
	{ "Terminator 2 - Judgement Day (1991)(Ocean Software).tap", 105018, 0x23bc34fc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTerminator2, SpecTerminator2, Spec128)
STD_ROM_FN(SpecTerminator2)

struct BurnDriver BurnSpecTerminator2 = {
	"spec_terminator2", NULL, "spec_spec128", NULL, "1991",
	"Terminator 2 - Judgement Day\0", NULL, "Ocean Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTerminator2RomInfo, SpecTerminator2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Karateka

static struct BurnRomInfo SpeckaratekaRomDesc[] = {
	{ "Karateka (1990)(Dro Soft).tap", 67058, 0x61d5c8ac, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speckarateka, Speckarateka, Spec128)
STD_ROM_FN(Speckarateka)

struct BurnDriver BurnSpeckarateka = {
	"spec_karateka", NULL, "spec_spec128", NULL, "1990",
	"Karateka\0", NULL, "Dro Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpeckaratekaRomInfo, SpeckaratekaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gunfright

static struct BurnRomInfo SpecGunfrightRomDesc[] = {
	{ "Gunfright (1986)(Ultimate Play The Game).tap", 43004, 0x2fe5588c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGunfright, SpecGunfright, Spec128)
STD_ROM_FN(SpecGunfright)

struct BurnDriver BurnSpecGunfright = {
	"spec_gunfright", NULL, "spec_spec128", NULL, "1986",
	"Gunfright\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGunfrightRomInfo, SpecGunfrightRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Arcadia

static struct BurnRomInfo SpecArcadiaRomDesc[] = {
	{ "Arcadia (1982)(Imagine)(16K).tap", 8522, 0x63ef88a9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecArcadia, SpecArcadia, Spec128)
STD_ROM_FN(SpecArcadia)

struct BurnDriver BurnSpecArcadia = {
	"spec_arcadia", NULL, "spec_spec128", NULL, "1982",
	"Arcadia\0", NULL, "Imagine", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecArcadiaRomInfo, SpecArcadiaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Batty

static struct BurnRomInfo SpecBattyRomDesc[] = {
	{ "Batty (1987)(Hit-Pak).tap", 30142, 0x83ed1c93, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBatty, SpecBatty, Spec128)
STD_ROM_FN(SpecBatty)

struct BurnDriver BurnSpecBatty = {
	"spec_batty", NULL, "spec_spec128", NULL, "1987",
	"Batty\0", NULL, "Hit-Pak", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBattyRomInfo, SpecBattyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cobra (48K)

static struct BurnRomInfo SpecCobraRomDesc[] = {
	{ "Cobra (1986)(Ocean).tap", 47827, 0x5b2ab724, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCobra, SpecCobra, Spectrum)
STD_ROM_FN(SpecCobra)

struct BurnDriver BurnSpecCobra = {
	"spec_cobra", NULL, "spec_spectrum", NULL, "1986",
	"Cobra (48K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecCobraRomInfo, SpecCobraRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cookie (16K)

static struct BurnRomInfo SpecCookieRomDesc[] = {
	{ "Cookie (1983)(Ultimate Play The Game)(16K).tap", 15646, 0xfffe62ca, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCookie, SpecCookie, Spec128)
STD_ROM_FN(SpecCookie)

struct BurnDriver BurnSpecCookie = {
	"spec_cookie", NULL, "spec_spec128", NULL, "1983",
	"Cookie (16K)\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecCookieRomInfo, SpecCookieRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Day in the Life, A

static struct BurnRomInfo SpecDayinthelifeRomDesc[] = {
	{ "Day in the Life, A (1985)(Micromega).tap", 50099, 0x80e1e221, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDayinthelife, SpecDayinthelife, Spec128)
STD_ROM_FN(SpecDayinthelife)

struct BurnDriver BurnSpecDayinthelife = {
	"spec_dayinthelife", NULL, "spec_spec128", NULL, "1985",
	"Day in the Life, A\0", NULL, "Micromega", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDayinthelifeRomInfo, SpecDayinthelifeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Firefly

static struct BurnRomInfo SpecFireflyRomDesc[] = {
	{ "Firefly (1988)(Ocean).tap", 45885, 0x4c890b6f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFirefly, SpecFirefly, Spec128)
STD_ROM_FN(SpecFirefly)

struct BurnDriver BurnSpecFirefly = {
	"spec_firefly", NULL, "spec_spec128", NULL, "1988",
	"Firefly\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFireflyRomInfo, SpecFireflyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gauntlet

static struct BurnRomInfo SpecGauntletRomDesc[] = {
	{ "Gauntlet (1987)(U.S. Gold)(48K-128K).tap", 150888, 0x173177cc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGauntlet, SpecGauntlet, Spec128)
STD_ROM_FN(SpecGauntlet)

struct BurnDriver BurnSpecGauntlet = {
	"spec_gauntlet", NULL, "spec_spec128", NULL, "1987",
	"Gauntlet\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGauntletRomInfo, SpecGauntletRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hyper Active

static struct BurnRomInfo SpecHyperactiveRomDesc[] = {
	{ "Hyper Active (1988)(Sinclair User).tap", 25305, 0xb7aa30e8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHyperactive, SpecHyperactive, Spec128)
STD_ROM_FN(SpecHyperactive)

struct BurnDriver BurnSpecHyperactive = {
	"spec_hyperactive", NULL, "spec_spec128", NULL, "1988",
	"Hyper Active\0", NULL, "Sinclair User", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHyperactiveRomInfo, SpecHyperactiveRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sabre Wulf

static struct BurnRomInfo SpecSabrewulfRomDesc[] = {
	{ "Sabre Wulf (1984)(Ultimate Play The Game).tap", 43105, 0x4aac52c1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSabrewulf, SpecSabrewulf, Spectrum)
STD_ROM_FN(SpecSabrewulf)

struct BurnDriver BurnSpecSabrewulf = {
	"spec_sabrewulf", NULL, "spec_spectrum", NULL, "1984",
	"Sabre Wulf\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSabrewulfRomInfo, SpecSabrewulfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Biz

static struct BurnRomInfo SpecThebizRomDesc[] = {
	{ "The Biz (1984)(Virgin Games).tap", 47268, 0x6773d204, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThebiz, SpecThebiz, Spec128)
STD_ROM_FN(SpecThebiz)

struct BurnDriver BurnSpecThebiz = {
	"spec_thebiz", NULL, "spec_spec128", NULL, "1984",
	"The Biz\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThebizRomInfo, SpecThebizRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Trashman

static struct BurnRomInfo SpecTrashmanRomDesc[] = {
	{ "Trashman (1984)(New Generation).tap", 33845, 0x79aac256, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTrashman, SpecTrashman, Spec128)
STD_ROM_FN(SpecTrashman)

struct BurnDriver BurnSpecTrashman = {
	"spec_trashman", NULL, "spec_spec128", NULL, "1984",
	"Trashman\0", NULL, "New Generation", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTrashmanRomInfo, SpecTrashmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aliens UK

static struct BurnRomInfo SpecAliensukRomDesc[] = {
	{ "Aliens UK (1986)(Electric Dreams).tap", 48291, 0xdb83e425, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAliensuk, SpecAliensuk, Spec128)
STD_ROM_FN(SpecAliensuk)

struct BurnDriver BurnSpecAliensuk = {
	"spec_aliensuk", NULL, "spec_spec128", NULL, "1986",
	"Aliens UK\0", NULL, "Electric Dreams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAliensukRomInfo, SpecAliensukRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aliens US

static struct BurnRomInfo SpecAliensusRomDesc[] = {
	{ "Aliens US (1987)(Electric Dreams).tap", 192788, 0xd7c22d0d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAliensus, SpecAliensus, Spec128)
STD_ROM_FN(SpecAliensus)

struct BurnDriver BurnSpecAliensus = {
	"spec_aliensus", NULL, "spec_spec128", NULL, "1987",
	"Aliens US\0", NULL, "Electric Dreams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAliensusRomInfo, SpecAliensusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Casanova

static struct BurnRomInfo SpecCasanovaRomDesc[] = {
	{ "Casanova (1989)(Iber Software).tap", 45005, 0x1c66d96d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCasanova, SpecCasanova, Spec128)
STD_ROM_FN(SpecCasanova)

struct BurnDriver BurnSpecCasanova = {
	"spec_casanova", NULL, "spec_spec128", NULL, "1989",
	"Casanova\0", NULL, "Iber Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCasanovaRomInfo, SpecCasanovaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Human Killing Machine

static struct BurnRomInfo SpecHumankillmachineRomDesc[] = {
	{ "Human Killing Machine (1988)(U.S. Gold).z80", 82727, 0xf2a03282, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHumankillmachine, SpecHumankillmachine, Spec128)
STD_ROM_FN(SpecHumankillmachine)

struct BurnDriver BurnSpecHumankillmachine = {
	"spec_humankillmachine", NULL, "spec_spec128", NULL, "1988",
	"Human Killing Machine\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHumankillmachineRomInfo, SpecHumankillmachineRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hypsys (48K) - Part 1

static struct BurnRomInfo SpecHypsys1RomDesc[] = {
	{ "Hypsys - Part 1 (1989)(Dro Soft).tap", 49162, 0x0d76e3f1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHypsys1, SpecHypsys1, Spectrum)
STD_ROM_FN(SpecHypsys1)

struct BurnDriver BurnSpecHypsys1 = {
	"spec_hypsys1", NULL, "spec_spectrum", NULL, "1989",
	"Hypsys (48K) - Part 1\0", NULL, "Dro Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecHypsys1RomInfo, SpecHypsys1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hypsys (48K) - Part 2

static struct BurnRomInfo SpecHypsys2RomDesc[] = {
	{ "Hypsys - Part 2 (1989)(Dro Soft).tap", 49809, 0x97b3d2d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHypsys2, SpecHypsys2, Spectrum)
STD_ROM_FN(SpecHypsys2)

struct BurnDriver BurnSpecHypsys2 = {
	"spec_hypsys2", "spec_hypsys1", "spec_spectrum", NULL, "1989",
	"Hypsys (48K) - Part 2\0", NULL, "Dro Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecHypsys2RomInfo, SpecHypsys2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Karnov

static struct BurnRomInfo SpecKarnovRomDesc[] = {
	{ "Karnov (1988)(Electric Dreams).tap", 234536, 0x95d92161, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKarnov, SpecKarnov, Spec128)
STD_ROM_FN(SpecKarnov)

struct BurnDriver BurnSpecKarnov = {
	"spec_karnov", NULL, "spec_spec128", NULL, "1988",
	"Karnov\0", NULL, "Electric Dreams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecKarnovRomInfo, SpecKarnovRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kendo Warrior (128K)

static struct BurnRomInfo SpecKendowarriorRomDesc[] = {
	{ "Kendo Warrior (1989)(Byte Back).tap", 48724, 0xb4d3cb13, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKendowarrior, SpecKendowarrior, Spec128)
STD_ROM_FN(SpecKendowarrior)

struct BurnDriver BurnSpecKendowarrior = {
	"spec_kendowarrior", NULL, "spec_spec128", NULL, "1989",
	"Kendo Warrior (128K)\0", NULL, "Byte Back", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecKendowarriorRomInfo, SpecKendowarriorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thunderbirds - Part 1

static struct BurnRomInfo SpecThundbirds1RomDesc[] = {
	{ "Thunderbirds - Part 1 (1989)(Grandslam Entertainments).tap", 52409, 0xa914189f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThundbirds1, SpecThundbirds1, Spec128)
STD_ROM_FN(SpecThundbirds1)

struct BurnDriver BurnSpecThundbirds1 = {
	"spec_thundbirds1", NULL, "spec_spec128", NULL, "1989",
	"Thunderbirds - Part 1\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThundbirds1RomInfo, SpecThundbirds1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thunderbirds - Part 2

static struct BurnRomInfo SpecThundbirds2RomDesc[] = {
	{ "Thunderbirds - Part 2 (1989)(Grandslam Entertainments).tap", 52409, 0x0869b04e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThundbirds2, SpecThundbirds2, Spec128)
STD_ROM_FN(SpecThundbirds2)

struct BurnDriver BurnSpecThundbirds2 = {
	"spec_thundbirds2", "spec_thundbirds1", "spec_spec128", NULL, "1989",
	"Thunderbirds - Part 2\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThundbirds2RomInfo, SpecThundbirds2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thunderbirds - Part 3

static struct BurnRomInfo SpecThundbirds3RomDesc[] = {
	{ "Thunderbirds - Part 3 (1989)(Grandslam Entertainments).tap", 52409, 0xb156bc82, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThundbirds3, SpecThundbirds3, Spec128)
STD_ROM_FN(SpecThundbirds3)

struct BurnDriver BurnSpecThundbirds3 = {
	"spec_thundbirds3", "spec_thundbirds1", "spec_spec128", NULL, "1989",
	"Thunderbirds - Part 3\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThundbirds3RomInfo, SpecThundbirds3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thunderbirds - Part 4

static struct BurnRomInfo SpecThundbirds4RomDesc[] = {
	{ "Thunderbirds - Part 4 (1989)(Grandslam Entertainments).tap", 52409, 0x9aafecf7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThundbirds4, SpecThundbirds4, Spec128)
STD_ROM_FN(SpecThundbirds4)

struct BurnDriver BurnSpecThundbirds4 = {
	"spec_thundbirds4", "spec_thundbirds1", "spec_spec128", NULL, "1989",
	"Thunderbirds - Part 4\0", NULL, "Grandslam Entertainments", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThundbirds4RomInfo, SpecThundbirds4RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Paradise Cafe

static struct BurnRomInfo SpecparadisecafeRomDesc[] = {
	{ "Paradise Cafe (1985)(Damatta).tap", 30777, 0xe6f5c3ea, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specparadisecafe, Specparadisecafe, Spec128)
STD_ROM_FN(Specparadisecafe)

struct BurnDriver BurnSpecparadisecafe = {
	"spec_paradisecafe", NULL, "spec_spec128", NULL, "1985",
	"Paradise Cafe\0", NULL, "Damatta", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecparadisecafeRomInfo, SpecparadisecafeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Xecutor

static struct BurnRomInfo SpecxecutorRomDesc[] = {
	{ "Xecutor (1987)(ACE Software).tap", 42063, 0xd1047062, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specxecutor, Specxecutor, Spec128)
STD_ROM_FN(Specxecutor)

struct BurnDriver BurnSpecxecutor = {
	"spec_xecutor", NULL, "spec_spec128", NULL, "1987",
	"Xecutor\0", NULL, "ACE Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecxecutorRomInfo, SpecxecutorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dominator

static struct BurnRomInfo SpecdominatorRomDesc[] = {
	{ "Dominator (1989)(System 3).tap", 67437, 0x3af9c9f7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdominator, Specdominator, Spec128)
STD_ROM_FN(Specdominator)

struct BurnDriver BurnSpecdominator = {
	"spec_dominator", NULL, "spec_spec128", NULL, "1987",
	"Dominator\0", NULL, "ACE Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecdominatorRomInfo, SpecdominatorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sword of the Samurai

static struct BurnRomInfo SpecSwordsamuraiRomDesc[] = {
	{ "Sword of the Samurai (1992)(Zeppelin Games Ltd).tap", 41439, 0x42e23e9f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSwordsamurai, SpecSwordsamurai, Spec128)
STD_ROM_FN(SpecSwordsamurai)

struct BurnDriver BurnSpecSwordsamurai = {
	"spec_swordsamurai", NULL, "spec_spec128", NULL, "1992",
	"Sword of the Samurai\0", NULL, "Zeppelin Games Ltd", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSwordsamuraiRomInfo, SpecSwordsamuraiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// California Games

static struct BurnRomInfo SpecCaliforniagamesRomDesc[] = {
	{ "California Games (1987)(U.S. Gold).tap", 168996, 0x52f1cd37, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCaliforniagames, SpecCaliforniagames, Spec128)
STD_ROM_FN(SpecCaliforniagames)

struct BurnDriver BurnSpecCaliforniagames = {
	"spec_californiagames", NULL, "spec_spec128", NULL, "1987",
	"California Games\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCaliforniagamesRomInfo, SpecCaliforniagamesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Deflektor

static struct BurnRomInfo SpecDeflektorRomDesc[] = {
	{ "Deflektor (1987)(Gremlin Graphics).tap", 28048, 0x04bf9aff, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDeflektor, SpecDeflektor, Spec128)
STD_ROM_FN(SpecDeflektor)

struct BurnDriver BurnSpecDeflektor = {
	"spec_deflektor", NULL, "spec_spec128", NULL, "1987",
	"Deflektor\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecDeflektorRomInfo, SpecDeflektorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Games - Summer Edition, The

static struct BurnRomInfo SpecGamessummeredRomDesc[] = {
	{ "Games - Summer Edition, The (1989)(U.S. Gold).tap", 354208, 0xe8b9a3d0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGamessummered, SpecGamessummered, Spec128)
STD_ROM_FN(SpecGamessummered)

struct BurnDriver BurnSpecGamessummered = {
	"spec_gamessummered", NULL, "spec_spec128", NULL, "1989",
	"Games - Summer Edition, The\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGamessummeredRomInfo, SpecGamessummeredRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Games - Winter Edition, The

static struct BurnRomInfo SpecGameswinteredRomDesc[] = {
	{ "Games - Winter Edition, The (1988)(U.S. Gold).tap", 146925, 0x15935233, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGameswintered, SpecGameswintered, Spec128)
STD_ROM_FN(SpecGameswintered)

struct BurnDriver BurnSpecGameswintered = {
	"spec_gameswintered", NULL, "spec_spec128", NULL, "1988",
	"Games - Winter Edition, The\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGameswinteredRomInfo, SpecGameswinteredRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Goonies, The

static struct BurnRomInfo SpecGooniesRomDesc[] = {
	{ "Goonies, The (1986)(U.S. Gold).tap", 48377, 0x9ed8efdf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGoonies, SpecGoonies, Spec128)
STD_ROM_FN(SpecGoonies)

struct BurnDriver BurnSpecGoonies = {
	"spec_goonies", NULL, "spec_spec128", NULL, "1986",
	"Goonies, The\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGooniesRomInfo, SpecGooniesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hyper Sports

static struct BurnRomInfo SpecHypersportsRomDesc[] = {
	{ "Hyper Sports (1985)(Imagine).tap", 48751, 0xe3a072a5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHypersports, SpecHypersports, Spec128)
STD_ROM_FN(SpecHypersports)

struct BurnDriver BurnSpecHypersports = {
	"spec_hypersports", NULL, "spec_spec128", NULL, "1985",
	"Hyper Sports\0", NULL, "Imagine", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHypersportsRomInfo, SpecHypersportsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lemmings

static struct BurnRomInfo SpecLemmingsRomDesc[] = {
	{ "Lemmings (1991)(Psygnosis).tap", 544963, 0x3a5fa27a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLemmings, SpecLemmings, Spec128)
STD_ROM_FN(SpecLemmings)

struct BurnDriver BurnSpecLemmings = {
	"spec_lemmings", NULL, "spec_spec128", NULL, "1991",
	"Lemmings\0", NULL, "Psygnosis", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecLemmingsRomInfo, SpecLemmingsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// P-47 Thunderbolt

static struct BurnRomInfo SpecP47thunderboltRomDesc[] = {
	{ "P-47 Thunderbolt (1990)(Firebird).z80", 72894, 0x9efaf7fc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecP47thunderbolt, SpecP47thunderbolt, Spec128)
STD_ROM_FN(SpecP47thunderbolt)

struct BurnDriver BurnSpecP47thunderbolt = {
	"spec_p47thunderbolt", NULL, "spec_spec128", NULL, "1990",
	"P-47 Thunderbolt\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecP47thunderboltRomInfo, SpecP47thunderboltRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Summer Games

static struct BurnRomInfo SpecSummergamesRomDesc[] = {
	{ "Summer Games (1988)(U.S. Gold).tap", 103495, 0xa03cb5cb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSummergames, SpecSummergames, Spec128)
STD_ROM_FN(SpecSummergames)

struct BurnDriver BurnSpecSummergames = {
	"spec_summergames", NULL, "spec_spec128", NULL, "1988",
	"Summer Games\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSummergamesRomInfo, SpecSummergamesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Summer Games II

static struct BurnRomInfo SpecSummergames2RomDesc[] = {
	{ "Summer Games II (1988)(U.S. Gold).tap", 189121, 0xdbe0691e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSummergames2, SpecSummergames2, Spec128)
STD_ROM_FN(SpecSummergames2)

struct BurnDriver BurnSpecSummergames2 = {
	"spec_summergames2", NULL, "spec_spec128", NULL, "1988",
	"Summer Games II\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSummergames2RomInfo, SpecSummergames2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Wonder Boy

static struct BurnRomInfo SpecSuperwonderboyRomDesc[] = {
	{ "Super Wonder Boy (1989)(Activision).tap", 169846, 0x179244d3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperwonderboy, SpecSuperwonderboy, Spec128)
STD_ROM_FN(SpecSuperwonderboy)

struct BurnDriver BurnSpecSuperwonderboy = {
	"spec_superwonderboy", NULL, "spec_spec128", NULL, "1989",
	"Super Wonder Boy\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperwonderboyRomInfo, SpecSuperwonderboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tai-Pan

static struct BurnRomInfo SpecTaipanRomDesc[] = {
	{ "Tai-Pan (1987)(Ocean).z80", 81683, 0x0ba89514, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTaipan, SpecTaipan, Spec128)
STD_ROM_FN(SpecTaipan)

struct BurnDriver BurnSpecTaipan = {
	"spec_taipan", NULL, "spec_spec128", NULL, "1987",
	"Tai-Pan\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTaipanRomInfo, SpecTaipanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// UN Squadron

static struct BurnRomInfo SpecUnsquadronRomDesc[] = {
	{ "UN Squadron (1990)(U.S Gold).tap", 303274, 0x12571e3a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecUnsquadron, SpecUnsquadron, Spec128)
STD_ROM_FN(SpecUnsquadron)

struct BurnDriver BurnSpecUnsquadron = {
	"spec_unsquadron", NULL, "spec_spec128", NULL, "1990",
	"UN Squadron\0", NULL, "U.S Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecUnsquadronRomInfo, SpecUnsquadronRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Winter Games

static struct BurnRomInfo SpecWintergamesRomDesc[] = {
	{ "Winter Games (1986)(U.S. Gold).tap", 119630, 0x5ab9f21c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWintergames, SpecWintergames, Spec128)
STD_ROM_FN(SpecWintergames)

struct BurnDriver BurnSpecWintergames = {
	"spec_wintergames", NULL, "spec_spec128", NULL, "1986",
	"Winter Games\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWintergamesRomInfo, SpecWintergamesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// World Games

static struct BurnRomInfo SpecWorldgamesRomDesc[] = {
	{ "World Games (1987)(U.S. Gold).tap", 224518, 0xab9ccddb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWorldgames, SpecWorldgames, Spec128)
STD_ROM_FN(SpecWorldgames)

struct BurnDriver BurnSpecWorldgames = {
	"spec_worldgames", NULL, "spec_spec128", NULL, "1987",
	"World Games\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWorldgamesRomInfo, SpecWorldgamesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Xecutor - Remix

static struct BurnRomInfo SpecXecutorremixRomDesc[] = {
	{ "Xecutor - Remix (1987)(ACE Software).z80", 77548, 0x97ef7538, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecXecutorremix, SpecXecutorremix, Spec128)
STD_ROM_FN(SpecXecutorremix)

struct BurnDriver BurnSpecXecutorremix = {
	"spec_xecutorremix", NULL, "spec_spec128", NULL, "1987",
	"Xecutor - Remix\0", NULL, "ACE Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecXecutorremixRomInfo, SpecXecutorremixRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 100 - Stolnik

static struct BurnRomInfo Spec100stolnikRomDesc[] = {
	{ "100 - Stolnik (1995)(Power Of Sound).z80", 43905, 0x8de30bdc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec100stolnik, Spec100stolnik, Spec128)
STD_ROM_FN(Spec100stolnik)

struct BurnDriver BurnSpec100stolnik = {
	"spec_100stolnik", NULL, "spec_spec128", NULL, "1995",
	"100 - Stolnik\0", NULL, "Power Of Sound", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, Spec100stolnikRomInfo, Spec100stolnikRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 10th Frame

static struct BurnRomInfo Spec10thframeRomDesc[] = {
	{ "10th Frame (1987)(U.S. Gold).tap", 20061, 0x4a6d1860, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec10thframe, Spec10thframe, Spec128)
STD_ROM_FN(Spec10thframe)

struct BurnDriver BurnSpec10thframe = {
	"spec_10thframe", NULL, "spec_spec128", NULL, "1987",
	"10th Frame\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, Spec10thframeRomInfo, Spec10thframeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 2112 AD

static struct BurnRomInfo Spec2112adRomDesc[] = {
	{ "2112 AD (1985)(Design Design).tap", 45602, 0xca5a76f1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec2112ad, Spec2112ad, Spec128)
STD_ROM_FN(Spec2112ad)

struct BurnDriver BurnSpec2112ad = {
	"spec_2112ad", NULL, "spec_spec128", NULL, "1985",
	"2112 AD\0", NULL, "Design Design", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec2112adRomInfo, Spec2112adRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 21 Erotic

static struct BurnRomInfo Spec21eroticRomDesc[] = {
	{ "21 Erotic (1995)(JardaSoft).tap", 10336, 0x80cb68c0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec21erotic, Spec21erotic, Spectrum)
STD_ROM_FN(Spec21erotic)

struct BurnDriver BurnSpec21erotic = {
	"spec_21erotic", NULL, "spec_spectrum", NULL, "1995",
	"21 Erotic\0", NULL, "JardaSoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec21eroticRomInfo, Spec21eroticRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 4 Soccer Simulators - 11-a-Side Soccer

static struct BurnRomInfo Spec4soccersims1RomDesc[] = {
	{ "4 Soccer Simulators - 11-a-Side Soccer (1989)(Codemasters).z80", 44128, 0x7409c0ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec4soccersims1, Spec4soccersims1, Spec128)
STD_ROM_FN(Spec4soccersims1)

struct BurnDriver BurnSpec4soccersims1 = {
	"spec_4soccersims1", NULL, "spec_spec128", NULL, "1989",
	"4 Soccer Simulators - 11-a-Side Soccer\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec4soccersims1RomInfo, Spec4soccersims1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 4 Soccer Simulators - Indoor Soccer

static struct BurnRomInfo Spec4soccersims2RomDesc[] = {
	{ "4 Soccer Simulators - Indoor Soccer (1989)(Codemasters).z80", 43850, 0xfa35acd5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec4soccersims2, Spec4soccersims2, Spec128)
STD_ROM_FN(Spec4soccersims2)

struct BurnDriver BurnSpec4soccersims2 = {
	"spec_4soccersims2", NULL, "spec_spec128", NULL, "1989",
	"4 Soccer Simulators - Indoor Soccer\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec4soccersims2RomInfo, Spec4soccersims2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 4 Soccer Simulators - Soccer Skills

static struct BurnRomInfo Spec4soccersims3RomDesc[] = {
	{ "4 Soccer Simulators - Soccer Skills (1989)(Codemasters).z80", 45681, 0x387466c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec4soccersims3, Spec4soccersims3, Spec128)
STD_ROM_FN(Spec4soccersims3)

struct BurnDriver BurnSpec4soccersims3 = {
	"spec_4soccersims3", NULL, "spec_spec128", NULL, "1989",
	"4 Soccer Simulators - Soccer Skills\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec4soccersims3RomInfo, Spec4soccersims3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 4 Soccer Simulators - Street Soccer

static struct BurnRomInfo Spec4soccersims4RomDesc[] = {
	{ "4 Soccer Simulators - Street Soccer (1989)(Codemasters).z80", 45164, 0xb2e7be10, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec4soccersims4, Spec4soccersims4, Spec128)
STD_ROM_FN(Spec4soccersims4)

struct BurnDriver BurnSpec4soccersims4 = {
	"spec_4soccersims4", NULL, "spec_spec128", NULL, "1989",
	"4 Soccer Simulators - Street Soccer\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec4soccersims4RomInfo, Spec4soccersims4RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 911 TS

static struct BurnRomInfo Spec911tsRomDesc[] = {
	{ "911 TS (1985)(Elite Systems).tap", 47921, 0x303db330, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec911ts, Spec911ts, Spec128)
STD_ROM_FN(Spec911ts)

struct BurnDriver BurnSpec911ts = {
	"spec_911ts", NULL, "spec_spec128", NULL, "1985",
	"911 TS\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec911tsRomInfo, Spec911tsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Advanced Pinball Simulator (128K)

static struct BurnRomInfo SpecAdvancedpinballsimRomDesc[] = {
	{ "Advanced Pinball Simulator (1990)(Codemasters).tap", 51760, 0xfca7d79b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAdvancedpinballsim, SpecAdvancedpinballsim, Spec128)
STD_ROM_FN(SpecAdvancedpinballsim)

struct BurnDriver BurnSpecAdvancedpinballsim = {
	"spec_advancedpinballsim", NULL, "spec_spec128", NULL, "1990",
	"Advanced Pinball Simulator (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 3, HARDWARE_SPECTRUM, GBF_PINBALL, 0,
	SpectrumGetZipName, SpecAdvancedpinballsimRomInfo, SpecAdvancedpinballsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Afterburner (128K)

static struct BurnRomInfo SpecAfterburnerRomDesc[] = {
	{ "Afterburner (1989)(Activision).tap", 102813, 0xb40696fb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAfterburner, SpecAfterburner, Spec128)
STD_ROM_FN(SpecAfterburner)

struct BurnDriver BurnSpecAfterburner = {
	"spec_afterburner", NULL, "spec_spec128", NULL, "1989",
	"Afterburner (128K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecAfterburnerRomInfo, SpecAfterburnerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alien 8 (48K)

static struct BurnRomInfo SpecAlien8RomDesc[] = {
	{ "Alien 8 (1985)(Ultimate Play The Game).tap", 47264, 0x043d8dae, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAlien8, SpecAlien8, Spectrum)
STD_ROM_FN(SpecAlien8)

struct BurnDriver BurnSpecAlien8 = {
	"spec_alien8", NULL, "spec_spectrum", NULL, "1985",
	"Alien 8 (48K)\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, SpecAlien8RomInfo, SpecAlien8RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Arcade Flight Simulator

static struct BurnRomInfo SpecArcadeflightsimRomDesc[] = {
	{ "Arcade Flight Simulator (1989)(Codemasters).tap", 48526, 0x834a6586, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecArcadeflightsim, SpecArcadeflightsim, Spec128)
STD_ROM_FN(SpecArcadeflightsim)

struct BurnDriver BurnSpecArcadeflightsim = {
	"spec_arcadeflightsim", NULL, "spec_spec128", NULL, "1989",
	"Arcade Flight Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecArcadeflightsimRomInfo, SpecArcadeflightsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Battle Ships

static struct BurnRomInfo SpecBattleshipsRomDesc[] = {
	{ "Battle Ships (1987)(Hit-Pak).z80", 35142, 0x1e72de5a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBattleships, SpecBattleships, Spec128)
STD_ROM_FN(SpecBattleships)

struct BurnDriver BurnSpecBattleships = {
	"spec_battleships", NULL, "spec_spec128", NULL, "1987",
	"Battle Ships\0", NULL, "Hit-Pak", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_STRATEGY, 0,
	SpectrumGetZipName, SpecBattleshipsRomInfo, SpecBattleshipsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bigfoot

static struct BurnRomInfo SpecBigfootRomDesc[] = {
	{ "Bigfoot (1988)(Codemasters).tap", 39134, 0x5f68a506, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBigfoot, SpecBigfoot, Spec128)
STD_ROM_FN(SpecBigfoot)

struct BurnDriver BurnSpecBigfoot = {
	"spec_bigfoot", NULL, "spec_spec128", NULL, "1988",
	"Bigfoot\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBigfootRomInfo, SpecBigfootRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Big Nose's American Adventure

static struct BurnRomInfo SpecBignoseamericanadvRomDesc[] = {
	{ "Big Nose's American Adventure (1992)(Codemasters).tap", 47470, 0x64006917, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBignoseamericanadv, SpecBignoseamericanadv, Spec128)
STD_ROM_FN(SpecBignoseamericanadv)

struct BurnDriver BurnSpecBignoseamericanadv = {
	"spec_bignoseamericanadv", NULL, "spec_spec128", NULL, "1992",
	"Big Nose's American Adventure\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBignoseamericanadvRomInfo, SpecBignoseamericanadvRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Big Trouble in Little China

static struct BurnRomInfo SpecBigtroubleinlittlechinaRomDesc[] = {
	{ "Big Trouble in Little China (1986)(Electric Dreams).tap", 51364, 0x5ba135d3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBigtroubleinlittlechina, SpecBigtroubleinlittlechina, Spec128)
STD_ROM_FN(SpecBigtroubleinlittlechina)

struct BurnDriver BurnSpecBigtroubleinlittlechina = {
	"spec_bigtroubleinlittlechina", NULL, "spec_spec128", NULL, "1986",
	"Big Trouble in Little China\0", NULL, "Electric Dreams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBigtroubleinlittlechinaRomInfo, SpecBigtroubleinlittlechinaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blade Warrior

static struct BurnRomInfo SpecBladewarriorRomDesc[] = {
	{ "Blade Warrior (1988)(Codemasters).tap", 41844, 0xd8bff571, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBladewarrior, SpecBladewarrior, Spec128)
STD_ROM_FN(SpecBladewarrior)

struct BurnDriver BurnSpecBladewarrior = {
	"spec_bladewarrior", NULL, "spec_spec128", NULL, "1988",
	"Blade Warrior\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBladewarriorRomInfo, SpecBladewarriorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// BMX Simulator 2 - Dirt Biking

static struct BurnRomInfo SpecBmxsim21RomDesc[] = {
	{ "BMX Simulator 2 - Dirt Biking (1989)(Codemasters).z80", 48248, 0xab7d26d5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBmxsim21, SpecBmxsim21, Spec128)
STD_ROM_FN(SpecBmxsim21)

struct BurnDriver BurnSpecBmxsim21 = {
	"spec_bmxsim21", NULL, "spec_spec128", NULL, "1989",
	"BMX Simulator 2 - Dirt Biking\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBmxsim21RomInfo, SpecBmxsim21RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// BMX Simulator 2 - Quarry Racing

static struct BurnRomInfo SpecBmxsim22RomDesc[] = {
	{ "BMX Simulator 2 - Quarry Racing (1989)(Codemasters).z80", 48362, 0xd75318bf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBmxsim22, SpecBmxsim22, Spec128)
STD_ROM_FN(SpecBmxsim22)

struct BurnDriver BurnSpecBmxsim22 = {
	"spec_bmxsim22", NULL, "spec_spec128", NULL, "1989",
	"BMX Simulator 2 - Quarry Racing\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBmxsim22RomInfo, SpecBmxsim22RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bounty Hunter (128K)

static struct BurnRomInfo SpecBountyhunterRomDesc[] = {
	{ "Bounty Hunter (1989)(Codemasters).tap", 47005, 0x91c9c3ba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBountyhunter, SpecBountyhunter, Spec128)
STD_ROM_FN(SpecBountyhunter)

struct BurnDriver BurnSpecBountyhunter = {
	"spec_bountyhunter", NULL, "spec_spec128", NULL, "1989",
	"Bounty Hunter (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecBountyhunterRomInfo, SpecBountyhunterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jackal

static struct BurnRomInfo SpecJackalRomDesc[] = {
	{ "Jackal (1986)(Konami).tap", 26861, 0x954fae85, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJackal, SpecJackal, Spectrum)
STD_ROM_FN(SpecJackal)

struct BurnDriver BurnSpecJackal = {
	"spec_jackal", NULL, "spec_spectrum", NULL, "1986",
	"Jackal\0", NULL, "Konami", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecJackalRomInfo, SpecJackalRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Laser Squad

static struct BurnRomInfo SpecLasersquadRomDesc[] = {
	{ "Laser Squad (1988)(Blade Software).z80", 123262, 0x2d11f296, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLasersquad, SpecLasersquad, Spec128)
STD_ROM_FN(SpecLasersquad)

struct BurnDriver BurnSpecLasersquad = {
	"spec_lasersquad", NULL, "spec_spec128", NULL, "1988",
	"Laser Squad\0", NULL, "Blade Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecLasersquadRomInfo, SpecLasersquadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Road Race

static struct BurnRomInfo SpecRoadraceRomDesc[] = {
	{ "Road Race (1987)(Your Sinclair).tap", 46100, 0xa1daf3de, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRoadrace, SpecRoadrace, Spec128)
STD_ROM_FN(SpecRoadrace)

struct BurnDriver BurnSpecRoadrace = {
	"spec_roadrace", NULL, "spec_spec128", NULL, "1987",
	"Road Race\0", NULL, "Your Sinclair", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRoadraceRomInfo, SpecRoadraceRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rod-Land (128K)

static struct BurnRomInfo SpecRodlandRomDesc[] = {
	{ "Rod-Land (1991)(Storm).tap", 117150, 0xcd3ecd05, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRodland, SpecRodland, Spec128)
STD_ROM_FN(SpecRodland)

struct BurnDriver BurnSpecRodland = {
	"spec_rodland", NULL, "spec_spec128", NULL, "1991",
	"Rod-Land (128K)\0", NULL, "Storm", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRodlandRomInfo, SpecRodlandRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shadow Of The Beast

static struct BurnRomInfo SpecShadowofthebeastRomDesc[] = {
	{ "Shadow Of The Beast (1990)(Gremlin Graphics).z80", 114104, 0x0507010d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecShadowofthebeast, SpecShadowofthebeast, Spec128)
STD_ROM_FN(SpecShadowofthebeast)

struct BurnDriver BurnSpecShadowofthebeast = {
	"spec_shadowofthebeast", NULL, "spec_spec128", NULL, "1990",
	"Shadow Of The Beast\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecShadowofthebeastRomInfo, SpecShadowofthebeastRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Space Crusade

static struct BurnRomInfo SpecSpacecrusadeRomDesc[] = {
	{ "Space Crusade (1992)(Gremlin Graphics).tap", 70110, 0xa46b2859, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSpacecrusade, SpecSpacecrusade, Spec128)
STD_ROM_FN(SpecSpacecrusade)

struct BurnDriver BurnSpecSpacecrusade = {
	"spec_spacecrusade", NULL, "spec_spec128", NULL, "1992",
	"Space Crusade\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSpacecrusadeRomInfo, SpecSpacecrusadeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Car Trans Am - American Turbo King

static struct BurnRomInfo SpecSupercartransamRomDesc[] = {
	{ "Super Car Trans Am - American Turbo King (1989)(Virgin Mastertronic).tap", 43374, 0xeb2361c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSupercartransam, SpecSupercartransam, Spec128)
STD_ROM_FN(SpecSupercartransam)

struct BurnDriver BurnSpecSupercartransam = {
	"spec_supercartransam", NULL, "spec_spec128", NULL, "1989",
	"Super Car Trans Am - American Turbo King\0", NULL, "Virgin Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSupercartransamRomInfo, SpecSupercartransamRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sword Slayer

static struct BurnRomInfo SpecSwordslayerRomDesc[] = {
	{ "Sword Slayer (1988)(Players).tap", 102652, 0x0a989566, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSwordslayer, SpecSwordslayer, Spec128)
STD_ROM_FN(SpecSwordslayer)

struct BurnDriver BurnSpecSwordslayer = {
	"spec_swordslayer", NULL, "spec_spec128", NULL, "1988",
	"Sword Slayer\0", NULL, "Players", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSwordslayerRomInfo, SpecSwordslayerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 1st Division Manager

static struct BurnRomInfo Spec1stdivmanagerRomDesc[] = {
	{ "1st Division Manager (1991)(Codemasters).z80", 47446, 0x57fed4fa, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec1stdivmanager, Spec1stdivmanager, Spec128)
STD_ROM_FN(Spec1stdivmanager)

struct BurnDriver BurnSpec1stdivmanager = {
	"spec_1stdivmanager", NULL, "spec_spec128", NULL, "1991",
	"1st Division Manager\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec1stdivmanagerRomInfo, Spec1stdivmanagerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 750cc Grand Prix (128K)

static struct BurnRomInfo Spec750ccRomDesc[] = {
	{ "750cc Grand Prix (1991)(Code Masters).tap", 82394, 0xc9073683, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec750cc, Spec750cc, Spec128)
STD_ROM_FN(Spec750cc)

struct BurnDriver BurnSpec750cc = {
	"spec_750cc", NULL, "spec_spec128", NULL, "1991",
	"750cc Grand Prix (128K)\0", NULL, "Code Masters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, Spec750ccRomInfo, Spec750ccRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Billy the Kid

static struct BurnRomInfo SpecBillythekidRomDesc[] = {
	{ "Billy the Kid (1989)(Virgin Mastertronic).tap", 34510, 0xbc4e886c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBillythekid, SpecBillythekid, Spec128)
STD_ROM_FN(SpecBillythekid)

struct BurnDriver BurnSpecBillythekid = {
	"spec_billythekid", NULL, "spec_spec128", NULL, "1989",
	"Billy the Kid\0", NULL, "Virgin Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBillythekidRomInfo, SpecBillythekidRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Brainache

static struct BurnRomInfo SpecBrainacheRomDesc[] = {
	{ "Brainache (1987)(Codemasters).tap", 49898, 0x28592736, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBrainache, SpecBrainache, Spec128)
STD_ROM_FN(SpecBrainache)

struct BurnDriver BurnSpecBrainache = {
	"spec_brainache", NULL, "spec_spec128", NULL, "1987",
	"Brainache\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBrainacheRomInfo, SpecBrainacheRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bronx Street Cop

static struct BurnRomInfo SpecBronxstcopRomDesc[] = {
	{ "Bronx Street Cop (1989)(Virgin Mastertronic).tap", 45318, 0x9884b2f8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBronxstcop, SpecBronxstcop, Spec128)
STD_ROM_FN(SpecBronxstcop)

struct BurnDriver BurnSpecBronxstcop = {
	"spec_bronxstcop", NULL, "spec_spec128", NULL, "1989",
	"Bronx Street Cop\0", NULL, "Virgin Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBronxstcopRomInfo, SpecBronxstcopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// By Fair Means...or Foul

static struct BurnRomInfo SpecByfairmeansorfoulRomDesc[] = {
	{ "By Fair Means Or Foul (1989)(Superior).z80", 44540, 0x8f8c6d0e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecByfairmeansorfoul, SpecByfairmeansorfoul, Spectrum)
STD_ROM_FN(SpecByfairmeansorfoul)

struct BurnDriver BurnSpecByfairmeansorfoul = {
	"spec_byfairmeansorfoul", NULL, "spec_spectrum", NULL, "1989",
	"By Fair Means...or Foul\0", NULL, "Superior", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecByfairmeansorfoulRomInfo, SpecByfairmeansorfoulRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Captain Dynamo

static struct BurnRomInfo SpecCaptaindynamoRomDesc[] = {
	{ "Captain Dynamo (1992)(Codemasters).tap", 68500, 0x62855d75, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCaptaindynamo, SpecCaptaindynamo, Spec128)
STD_ROM_FN(SpecCaptaindynamo)

struct BurnDriver BurnSpecCaptaindynamo = {
	"spec_captaindynamo", NULL, "spec_spec128", NULL, "1992",
	"Captain Dynamo\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCaptaindynamoRomInfo, SpecCaptaindynamoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cue Boy

static struct BurnRomInfo SpecCueboyRomDesc[] = {
	{ "Cue Boy (1992)(Codemasters).tap", 54475, 0x2b625a7e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCueboy, SpecCueboy, Spec128)
STD_ROM_FN(SpecCueboy)

struct BurnDriver BurnSpecCueboy = {
	"spec_cueboy", NULL, "spec_spec128", NULL, "1992",
	"Cue Boy\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCueboyRomInfo, SpecCueboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Death Stalker

static struct BurnRomInfo SpecDeathstalkerRomDesc[] = {
	{ "Death Stalker (1988)(Codemasters).tap", 47764, 0xfcd48073, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDeathstalker, SpecDeathstalker, Spectrum)
STD_ROM_FN(SpecDeathstalker)

struct BurnDriver BurnSpecDeathstalker = {
	"spec_deathstalker", NULL, "spec_spectrum", NULL, "1988",
	"Death Stalker\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDeathstalkerRomInfo, SpecDeathstalkerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy 3 & a Half

static struct BurnRomInfo SpecDizzy3andahalfRomDesc[] = {
	{ "Dizzy 3 & a Half (1988)(Codemasters).tap", 25241, 0x8a82c837, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDizzy3andahalf, SpecDizzy3andahalf, Spec128)
STD_ROM_FN(SpecDizzy3andahalf)

struct BurnDriver BurnSpecDizzy3andahalf = {
	"spec_dizzy3andahalf", NULL, "spec_spec128", NULL, "1988",
	"Dizzy 3 & a Half\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecDizzy3andahalfRomInfo, SpecDizzy3andahalfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// DJ Puff

static struct BurnRomInfo SpecDjpuffRomDesc[] = {
	{ "DJ Puff (1992)(Codemasters).z80", 66937, 0x38340567, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDjpuff, SpecDjpuff, Spec128)
STD_ROM_FN(SpecDjpuff)

struct BurnDriver BurnSpecDjpuff = {
	"spec_djpuff", NULL, "spec_spec128", NULL, "1992",
	"DJ Puff\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDjpuffRomInfo, SpecDjpuffRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// F-16 Fighting Falcon

static struct BurnRomInfo SpecF16falconRomDesc[] = {
	{ "F-16 Fighting Falcon (1990)(Virgin Mastertronic).tap", 39117, 0x2071bb7c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecF16falcon, SpecF16falcon, Spec128)
STD_ROM_FN(SpecF16falcon)

struct BurnDriver BurnSpecF16falcon = {
	"spec_f16falcon", NULL, "spec_spec128", NULL, "1990",
	"F-16 Fighting Falcon\0", NULL, "Virgin Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecF16falconRomInfo, SpecF16falconRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fairlight

static struct BurnRomInfo SpecFairlightRomDesc[] = {
	{ "Fairlight (1985)(The Edge).tap", 80684, 0xf8175672, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFairlight, SpecFairlight, Spec128)
STD_ROM_FN(SpecFairlight)

struct BurnDriver BurnSpecFairlight = {
	"spec_fairlight", NULL, "spec_spec128", NULL, "1985",
	"Fairlight\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFairlightRomInfo, SpecFairlightRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fairlight II

static struct BurnRomInfo SpecFairlight2RomDesc[] = {
	{ "Fairlight II (1986)(The Edge).z80", 72704, 0x28eb4e46, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFairlight2, SpecFairlight2, Spec128)
STD_ROM_FN(SpecFairlight2)

struct BurnDriver BurnSpecFairlight2 = {
	"spec_fairlight2", NULL, "spec_spec128", NULL, "1986",
	"Fairlight II\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFairlight2RomInfo, SpecFairlight2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Frankenstein Jnr

static struct BurnRomInfo SpecFrankensteinjrRomDesc[] = {
	{ "Frankenstein Jnr (1990)(Codemasters).tap", 43412, 0xf549d96d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFrankensteinjr, SpecFrankensteinjr, Spec128)
STD_ROM_FN(SpecFrankensteinjr)

struct BurnDriver BurnSpecFrankensteinjr = {
	"spec_frankensteinjr", NULL, "spec_spec128", NULL, "1990",
	"Frankenstein Jnr\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFrankensteinjrRomInfo, SpecFrankensteinjrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Freddy Hardest in South Manhattan

static struct BurnRomInfo SpecFreddyhardestsouthmanRomDesc[] = {
	{ "Freddy Hardest in South Manhattan (1989)(Dinamic Software).tap", 61774, 0xffbc6590, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFreddyhardestsouthman, SpecFreddyhardestsouthman, Spec128)
STD_ROM_FN(SpecFreddyhardestsouthman)

struct BurnDriver BurnSpecFreddyhardestsouthman = {
	"spec_freddyhardestsouthman", NULL, "spec_spec128", NULL, "1989",
	"Freddy Hardest in South Manhattan\0", NULL, "Dinamic Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFreddyhardestsouthmanRomInfo, SpecFreddyhardestsouthmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fruit Machine Simulator - Cash Bash

static struct BurnRomInfo SpecFruitmachinesim1RomDesc[] = {
	{ "Fruit Machine Simulator - Cash Bash (1987)(Codemasters).tap", 47967, 0x0f58266f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFruitmachinesim1, SpecFruitmachinesim1, Spectrum)
STD_ROM_FN(SpecFruitmachinesim1)

struct BurnDriver BurnSpecFruitmachinesim1 = {
	"spec_fruitmachinesim1", NULL, "spec_spectrum", NULL, "1987",
	"Fruit Machine Simulator - Cash Bash\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFruitmachinesim1RomInfo, SpecFruitmachinesim1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fruit Machine Simulator 2 - Mega Trek

static struct BurnRomInfo SpecFruitmachinesim2RomDesc[] = {
	{ "Fruit Machine Simulator 2 - Mega Trek (1990)(Codemasters).tap", 49545, 0xbb3c551e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFruitmachinesim2, SpecFruitmachinesim2, Spec128)
STD_ROM_FN(SpecFruitmachinesim2)

struct BurnDriver BurnSpecFruitmachinesim2 = {
	"spec_fruitmachinesim2", NULL, "spec_spec128", NULL, "1990",
	"Fruit Machine Simulator 2 - Mega Trek\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFruitmachinesim2RomInfo, SpecFruitmachinesim2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ghost Hunters

static struct BurnRomInfo SpecGhosthuntersRomDesc[] = {
	{ "Ghost Hunters (1987)(Codemasters).tap", 48896, 0xdfa7ffcb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGhosthunters, SpecGhosthunters, Spectrum)
STD_ROM_FN(SpecGhosthunters)

struct BurnDriver BurnSpecGhosthunters = {
	"spec_ghosthunters", NULL, "spec_spectrum", NULL, "1987",
	"Ghost Hunters\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGhosthuntersRomInfo, SpecGhosthuntersRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Grand Prix Simulator

static struct BurnRomInfo SpecGpsimulator1RomDesc[] = {
	{ "Grand Prix Simulator (1987)(Codemasters).tap", 55033, 0x379b30e6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGpsimulator1, SpecGpsimulator1, Spec128)
STD_ROM_FN(SpecGpsimulator1)

struct BurnDriver BurnSpecGpsimulator1 = {
	"spec_gpsimulator1", NULL, "spec_spec128", NULL, "1987",
	"Grand Prix Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGpsimulator1RomInfo, SpecGpsimulator1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Grand Prix Simulator 2

static struct BurnRomInfo SpecGpsimulator2RomDesc[] = {
	{ "Grand Prix Simulator 2 (1989)(Codemasters).z80", 49299, 0xa786116a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGpsimulator2, SpecGpsimulator2, Spec128)
STD_ROM_FN(SpecGpsimulator2)

struct BurnDriver BurnSpecGpsimulator2 = {
	"spec_gpsimulator2", NULL, "spec_spec128", NULL, "1989",
	"Grand Prix Simulator 2\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGpsimulator2RomInfo, SpecGpsimulator2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Grell and Falla

static struct BurnRomInfo SpecGrellandfallaRomDesc[] = {
	{ "Grell and Falla (1987)(Codemasters).z80", 92473, 0x8929d289, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGrellandfalla, SpecGrellandfalla, Spec128)
STD_ROM_FN(SpecGrellandfalla)

struct BurnDriver BurnSpecGrellandfalla = {
	"spec_grellandfalla", NULL, "spec_spec128", NULL, "1987",
	"Grell and Falla\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGrellandfallaRomInfo, SpecGrellandfallaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hit Squad, The

static struct BurnRomInfo SpecHitsquadRomDesc[] = {
	{ "Hit Squad, The (1988)(Codemasters).z80", 40124, 0x8afa17ab, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHitsquad, SpecHitsquad, Spectrum)
STD_ROM_FN(SpecHitsquad)

struct BurnDriver BurnSpecHitsquad = {
	"spec_hitsquad", NULL, "spec_spectrum", NULL, "1988",
	"Hit Squad, The\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHitsquadRomInfo, SpecHitsquadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hundra

static struct BurnRomInfo SpecHundraRomDesc[] = {
	{ "Hundra (1988)(Mastertronic).tap", 48229, 0xeeb32b33, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHundra, SpecHundra, Spec128)
STD_ROM_FN(SpecHundra)

struct BurnDriver BurnSpecHundra = {
	"spec_hundra", NULL, "spec_spec128", NULL, "1988",
	"Hundra\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHundraRomInfo, SpecHundraRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// International 3D Tennis (128K)

static struct BurnRomInfo SpecInt3dtennisRomDesc[] = {
	{ "International 3D Tennis (1990)(Palace).tap", 85714, 0x6d60e471, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecInt3dtennis, SpecInt3dtennis, Spec128)
STD_ROM_FN(SpecInt3dtennis)

struct BurnDriver BurnSpecInt3dtennis = {
	"spec_int3dtennis", NULL, "spec_spec128", NULL, "1990",
	"International 3D Tennis (128K)\0", NULL, "Palace", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_SPORTSMISC, 0,
	SpectrumGetZipName, SpecInt3dtennisRomInfo, SpecInt3dtennisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// International Rugby

static struct BurnRomInfo SpecIntrugbyRomDesc[] = {
	{ "International Rugby (1987)(Codemasters).tap", 40161, 0xbaf57b6c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecIntrugby, SpecIntrugby, Spectrum)
STD_ROM_FN(SpecIntrugby)

struct BurnDriver BurnSpecIntrugby = {
	"spec_intrugby", NULL, "spec_spectrum", NULL, "1987",
	"International Rugby\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecIntrugbyRomInfo, SpecIntrugbyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// International Speedway

static struct BurnRomInfo SpecIntspeedwayRomDesc[] = {
	{ "International Speedway (1988)(Silverbird).z80", 35832, 0x7d1e8f3a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecIntspeedway, SpecIntspeedway, Spec128)
STD_ROM_FN(SpecIntspeedway)

struct BurnDriver BurnSpecIntspeedway = {
	"spec_intspeedway", NULL, "spec_spec128", NULL, "1988",
	"International Speedway\0", NULL, "Silverbird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecIntspeedwayRomInfo, SpecIntspeedwayRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Italian Supercar

static struct BurnRomInfo SpecItaliansupercarRomDesc[] = {
	{ "Italian Supercar (1990)(Codemasters).tap", 48649, 0x0e28c70e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecItaliansupercar, SpecItaliansupercar, Spec128)
STD_ROM_FN(SpecItaliansupercar)

struct BurnDriver BurnSpecItaliansupercar = {
	"spec_italiansupercar", NULL, "spec_spec128", NULL, "1990",
	"Italian Supercar\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecItaliansupercarRomInfo, SpecItaliansupercarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Bike Simulator - Lakes Standard

static struct BurnRomInfo SpecJetbikesim1RomDesc[] = {
	{ "Jet Bike Simulator - Lakes Standard (1988)(Codemasters).z80", 37457, 0x4732d30c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetbikesim1, SpecJetbikesim1, Spectrum)
STD_ROM_FN(SpecJetbikesim1)

struct BurnDriver BurnSpecJetbikesim1 = {
	"spec_jetbikesim1", NULL, "spec_spectrum", NULL, "1988",
	"Jet Bike Simulator - Lakes Standard\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecJetbikesim1RomInfo, SpecJetbikesim1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Bike Simulator - Coasts Standard

static struct BurnRomInfo SpecJetbikesim2RomDesc[] = {
	{ "Jet Bike Simulator - Coasts Standard (1988)(Codemasters).z80", 37757, 0x2311bdb7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetbikesim2, SpecJetbikesim2, Spectrum)
STD_ROM_FN(SpecJetbikesim2)

struct BurnDriver BurnSpecJetbikesim2 = {
	"spec_jetbikesim2", NULL, "spec_spectrum", NULL, "1988",
	"Jet Bike Simulator - Coasts Standard\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecJetbikesim2RomInfo, SpecJetbikesim2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Bike Simulator - Docks Standard

static struct BurnRomInfo SpecJetbikesim3RomDesc[] = {
	{ "Jet Bike Simulator - Docks Standard (1988)(Codemasters).z80", 37266, 0x94a6c7d8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetbikesim3, SpecJetbikesim3, Spectrum)
STD_ROM_FN(SpecJetbikesim3)

struct BurnDriver BurnSpecJetbikesim3 = {
	"spec_jetbikesim3", NULL, "spec_spectrum", NULL, "1988",
	"Jet Bike Simulator - Docks Standard\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecJetbikesim3RomInfo, SpecJetbikesim3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Bike Simulator - Lakes Expert

static struct BurnRomInfo SpecJetbikesim4RomDesc[] = {
	{ "Jet Bike Simulator - Lakes Expert (1988)(Codemasters).z80", 37906, 0x3681dac9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetbikesim4, SpecJetbikesim4, Spectrum)
STD_ROM_FN(SpecJetbikesim4)

struct BurnDriver BurnSpecJetbikesim4 = {
	"spec_jetbikesim4", "spec_jetbikesim1", "spec_spectrum", NULL, "1988",
	"Jet Bike Simulator - Lakes Expert\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecJetbikesim4RomInfo, SpecJetbikesim4RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Bike Simulator - Coasts Expert

static struct BurnRomInfo SpecJetbikesim5RomDesc[] = {
	{ "Jet Bike Simulator - Coasts Expert (1988)(Codemasters).z80", 38206, 0xc3d570e4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetbikesim5, SpecJetbikesim5, Spectrum)
STD_ROM_FN(SpecJetbikesim5)

struct BurnDriver BurnSpecJetbikesim5 = {
	"spec_jetbikesim5", "spec_jetbikesim2", "spec_spectrum", NULL, "1988",
	"Jet Bike Simulator - Coasts Expert\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecJetbikesim5RomInfo, SpecJetbikesim5RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Bike Simulator - Docks Expert

static struct BurnRomInfo SpecJetbikesim6RomDesc[] = {
	{ "Jet Bike Simulator - Docks Expert (1988)(Codemasters).z80", 37673, 0x8e1e9ee5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetbikesim6, SpecJetbikesim6, Spectrum)
STD_ROM_FN(SpecJetbikesim6)

struct BurnDriver BurnSpecJetbikesim6 = {
	"spec_jetbikesim6", "spec_jetbikesim3", "spec_spectrum", NULL, "1988",
	"Jet Bike Simulator - Docks Expert\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 2, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecJetbikesim6RomInfo, SpecJetbikesim6RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jungle Warfare

static struct BurnRomInfo SpecJunglewarfareRomDesc[] = {
	{ "Jungle Warfare (1989)(Codemasters).tap", 43631, 0xeb977230, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJunglewarfare, SpecJunglewarfare, Spec128)
STD_ROM_FN(SpecJunglewarfare)

struct BurnDriver BurnSpecJunglewarfare = {
	"spec_junglewarfare", NULL, "spec_spec128", NULL, "1989",
	"Jungle Warfare\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecJunglewarfareRomInfo, SpecJunglewarfareRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kamikaze

static struct BurnRomInfo SpecKamikazeRomDesc[] = {
	{ "Kamikaze (1991)(Codemasters).tap", 47767, 0xd35b3009, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKamikaze, SpecKamikaze, Spec128)
STD_ROM_FN(SpecKamikaze)

struct BurnDriver BurnSpecKamikaze = {
	"spec_kamikaze", NULL, "spec_spec128", NULL, "1991",
	"Kamikaze\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecKamikazeRomInfo, SpecKamikazeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// KGB Super Spy

static struct BurnRomInfo SpecKgbsuperspyRomDesc[] = {
	{ "KGB Super Spy (1989)(Codemasters).tap", 45049, 0xa879a856, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKgbsuperspy, SpecKgbsuperspy, Spectrum)
STD_ROM_FN(SpecKgbsuperspy)

struct BurnDriver BurnSpecKgbsuperspy = {
	"spec_kgbsuperspy", NULL, "spec_spectrum", NULL, "1989",
	"KGB Super Spy\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecKgbsuperspyRomInfo, SpecKgbsuperspyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Escape from the Planet of the Robot Monsters

static struct BurnRomInfo SpecEscplanetrobotmonRomDesc[] = {
	{ "Escape from the Planet of the Robot Monsters (1990)(Domark).tap", 111578, 0x9f95584c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEscplanetrobotmon, SpecEscplanetrobotmon, Spec128)
STD_ROM_FN(SpecEscplanetrobotmon)

struct BurnDriver BurnSpecEscplanetrobotmon = {
	"spec_escplanetrobotmon", NULL, "spec_spec128", NULL, "1990",
	"Escape from the Planet of the Robot Monsters\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecEscplanetrobotmonRomInfo, SpecEscplanetrobotmonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gauntlet II

static struct BurnRomInfo SpecGauntlet2RomDesc[] = {
	{ "Gauntlet II (1987)(U.S. Gold).tap", 135163, 0x400e869d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGauntlet2, SpecGauntlet2, Spec128)
STD_ROM_FN(SpecGauntlet2)

struct BurnDriver BurnSpecGauntlet2 = {
	"spec_gauntlet2", NULL, "spec_spec128", NULL, "1987",
	"Gauntlet II\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGauntlet2RomInfo, SpecGauntlet2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Last Vampire, The

static struct BurnRomInfo SpecLastvampireRomDesc[] = {
	{ "Last Vampire, The (1990)(Atlantis).tap", 50418, 0x9c48dd1a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLastvampire, SpecLastvampire, Spec128)
STD_ROM_FN(SpecLastvampire)

struct BurnDriver BurnSpecLastvampire = {
	"spec_lastvampire", NULL, "spec_spec128", NULL, "1990",
	"Last Vampire, The\0", NULL, "Atlantis", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecLastvampireRomInfo, SpecLastvampireRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Michel Futbol Master - Championship

static struct BurnRomInfo SpecMichelfutbolchampRomDesc[] = {
	{ "Michel Futbol Master - Championship (1989)(Dinamic).z80", 41534, 0x893f9bf7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMichelfutbolchamp, SpecMichelfutbolchamp, Spec128)
STD_ROM_FN(SpecMichelfutbolchamp)

struct BurnDriver BurnSpecMichelfutbolchamp = {
	"spec_michelfutbolchamp", NULL, "spec_spec128", NULL, "1989",
	"Michel Futbol Master - Championship\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMichelfutbolchampRomInfo, SpecMichelfutbolchampRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Michel Futbol Master - Super Skills

static struct BurnRomInfo SpecMichelfutbolskillsRomDesc[] = {
	{ "Michel Futbol Master - Super Skills (1989)(Dinamic).z80", 42768, 0x66b32077, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMichelfutbolskills, SpecMichelfutbolskills, Spec128)
STD_ROM_FN(SpecMichelfutbolskills)

struct BurnDriver BurnSpecMichelfutbolskills = {
	"spec_michelfutbolskills", NULL, "spec_spec128", NULL, "1989",
	"Michel Futbol Master - Super Skills\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMichelfutbolskillsRomInfo, SpecMichelfutbolskillsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// NARC

static struct BurnRomInfo SpecNarcRomDesc[] = {
	{ "NARC (1990)(Ocean).tap", 356350, 0x3c9ccc9c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNarc, SpecNarc, Spec128)
STD_ROM_FN(SpecNarc)

struct BurnDriver BurnSpecNarc = {
	"spec_narc", NULL, "spec_spec128", NULL, "1990",
	"NARC\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecNarcRomInfo, SpecNarcRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Simpsons - Bart vs. the Space Mutants, The

static struct BurnRomInfo SpecSimpsonsbartvsspacemutRomDesc[] = {
	{ "Simpsons - Bart vs. the Space Mutants, The (1991)(Ocean).tap", 351987, 0x382137b2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSimpsonsbartvsspacemut, SpecSimpsonsbartvsspacemut, Spec128)
STD_ROM_FN(SpecSimpsonsbartvsspacemut)

struct BurnDriver BurnSpecSimpsonsbartvsspacemut = {
	"spec_simpsonsbartvsspacemut", NULL, "spec_spec128", NULL, "1991",
	"Simpsons - Bart vs. the Space Mutants, The\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSimpsonsbartvsspacemutRomInfo, SpecSimpsonsbartvsspacemutRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Skateball

static struct BurnRomInfo SpecSkateballRomDesc[] = {
	{ "Skateball (1988)(Ubi Soft).tap", 49399, 0xe816c833, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSkateball, SpecSkateball, Spec128)
STD_ROM_FN(SpecSkateball)

struct BurnDriver BurnSpecSkateball = {
	"spec_skateball", NULL, "spec_spec128", NULL, "1988",
	"Skateball\0", NULL, "Ubi Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSkateballRomInfo, SpecSkateballRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sly Spy - Secret Agent

static struct BurnRomInfo SpecSlyspyRomDesc[] = {
	{ "Sly Spy - Secret Agent (1990)(Ocean).tap", 170067, 0x1276744e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSlyspy, SpecSlyspy, Spec128)
STD_ROM_FN(SpecSlyspy)

struct BurnDriver BurnSpecSlyspy = {
	"spec_slyspy", NULL, "spec_spec128", NULL, "1990",
	"Sly Spy - Secret Agent\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSlyspyRomInfo, SpecSlyspyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Steg the Slug (128K)

static struct BurnRomInfo SpecStegRomDesc[] = {
	{ "Steg (1992)(Codemasters).tap", 51271, 0x15a4d49e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSteg, SpecSteg, Spec128)
STD_ROM_FN(SpecSteg)

struct BurnDriver BurnSpecSteg = {
	"spec_steg", NULL, "spec_spec128", NULL, "1992",
	"Steg the Slug (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecStegRomInfo, SpecStegRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Toobin'

static struct BurnRomInfo SpecToobinRomDesc[] = {
	{ "Toobin' (1989)(Domark).tap", 53121, 0xa2acce35, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecToobin, SpecToobin, Spec128)
STD_ROM_FN(SpecToobin)

struct BurnDriver BurnSpecToobin = {
	"spec_toobin", NULL, "spec_spec128", NULL, "1989",
	"Toobin'\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecToobinRomInfo, SpecToobinRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// V (Visitors)

static struct BurnRomInfo SpecVoceanRomDesc[] = {
	{ "V (1986)(Ocean).tap", 56344, 0x07f2dfd6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecVocean, SpecVocean, Spec128)
STD_ROM_FN(SpecVocean)

struct BurnDriver BurnSpecVocean = {
	"spec_vocean", NULL, "spec_spec128", NULL, "1986",
	"V (Visitors)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecVoceanRomInfo, SpecVoceanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Where Time Stood Still

static struct BurnRomInfo SpecWheretimestoodstillRomDesc[] = {
	{ "Where Time Stood Still (1988)(Ocean).tap", 110751, 0xfa89cab1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWheretimestoodstill, SpecWheretimestoodstill, Spec128)
STD_ROM_FN(SpecWheretimestoodstill)

struct BurnDriver BurnSpecWheretimestoodstill = {
	"spec_wheretimestoodstill", NULL, "spec_spec128", NULL, "1988",
	"Where Time Stood Still\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWheretimestoodstillRomInfo, SpecWheretimestoodstillRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wizball (128K)

static struct BurnRomInfo SpecWizballRomDesc[] = {
	{ "Wizball (1987)(Ocean).tap", 34256, 0x70cf4de6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWizball, SpecWizball, Spec128)
STD_ROM_FN(SpecWizball)

struct BurnDriver BurnSpecWizball = {
	"spec_wizball", NULL, "spec_spec128", NULL, "1987",
	"Wizball (128K)\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecWizballRomInfo, SpecWizballRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aaargh!

static struct BurnRomInfo SpecAaarghRomDesc[] = {
	{ "Aaargh! (1989)(Binary Design).tap", 96865, 0x11417dd7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAaargh, SpecAaargh, Spec128)
STD_ROM_FN(SpecAaargh)

struct BurnDriver BurnSpecAaargh = {
	"spec_aaargh", NULL, "spec_spec128", NULL, "1989",
	"Aaargh!\0", NULL, "Binary Design", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAaarghRomInfo, SpecAaarghRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Agent Orange

static struct BurnRomInfo SpecAgentorangeRomDesc[] = {
	{ "Agent Orange (1987)(Icon Design).tap", 43566, 0x112aedd9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAgentorange, SpecAgentorange, Spec128)
STD_ROM_FN(SpecAgentorange)

struct BurnDriver BurnSpecAgentorange = {
	"spec_agentorange", NULL, "spec_spec128", NULL, "1987",
	"Agent Orange\0", NULL, "Icon Design", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAgentorangeRomInfo, SpecAgentorangeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ah Diddums

static struct BurnRomInfo SpecAhdiddumsRomDesc[] = {
	{ "Ah Diddums (1983)(Imagine).tap", 8490, 0xe5b88c3b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAhdiddums, SpecAhdiddums, Spec128)
STD_ROM_FN(SpecAhdiddums)

struct BurnDriver BurnSpecAhdiddums = {
	"spec_ahdiddums", NULL, "spec_spec128", NULL, "1983",
	"Ah Diddums\0", NULL, "Imagine", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAhdiddumsRomInfo, SpecAhdiddumsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alien Evolution

static struct BurnRomInfo SpecAlienevolutionRomDesc[] = {
	{ "Alien Evolution (1987)(Gremlin).tap", 49510, 0xa16b45f5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAlienevolution, SpecAlienevolution, Spec128)
STD_ROM_FN(SpecAlienevolution)

struct BurnDriver BurnSpecAlienevolution = {
	"spec_alienevolution", NULL, "spec_spec128", NULL, "1987",
	"Alien Evolution\0", NULL, "Gremlin", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAlienevolutionRomInfo, SpecAlienevolutionRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aqua Plane

static struct BurnRomInfo SpecAquaplaneRomDesc[] = {
	{ "Aqua Plane (1983)(Quicksilva).tap", 13553, 0x3ef11315, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAquaplane, SpecAquaplane, Spec128)
STD_ROM_FN(SpecAquaplane)

struct BurnDriver BurnSpecAquaplane = {
	"spec_aquaplane", NULL, "spec_spec128", NULL, "1983",
	"Aqua Plane\0", NULL, "Quicksilva", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAquaplaneRomInfo, SpecAquaplaneRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Atom Ant (128K)

static struct BurnRomInfo SpecAtomantRomDesc[] = {
	{ "Atom Ant (1990)(Hi-Tec).tap", 61884, 0xbb656d10, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAtomant, SpecAtomant, Spec128)
STD_ROM_FN(SpecAtomant)

struct BurnDriver BurnSpecAtomant = {
	"spec_atomant", NULL, "spec_spec128", NULL, "1990",
	"Atom Ant (128K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAtomantRomInfo, SpecAtomantRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Autocrash

static struct BurnRomInfo SpecAutocrashRomDesc[] = {
	{ "Autocrash (1991)(Zigurat).tap", 49550, 0x02fe192c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAutocrash, SpecAutocrash, Spec128)
STD_ROM_FN(SpecAutocrash)

struct BurnDriver BurnSpecAutocrash = {
	"spec_autocrash", NULL, "spec_spec128", NULL, "1991",
	"Autocrash\0", NULL, "Zigurat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAutocrashRomInfo, SpecAutocrashRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// B.C. Bill

static struct BurnRomInfo SpecBcbillRomDesc[] = {
	{ "B.C. Bill (1984)(Imagine).tap", 30496, 0x61b2d9bf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBcbill, SpecBcbill, Spec128)
STD_ROM_FN(SpecBcbill)

struct BurnDriver BurnSpecBcbill = {
	"spec_bcbill", NULL, "spec_spec128", NULL, "1984",
	"B.C. Bill\0", NULL, "Imagine", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBcbillRomInfo, SpecBcbillRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// BC's Quest for Tires

static struct BurnRomInfo SpecBcquesttiresRomDesc[] = {
	{ "BC's Quest for Tires (1985)(Software Projects).tap", 43450, 0xfcb53b87, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBcquesttires, SpecBcquesttires, Spec128)
STD_ROM_FN(SpecBcquesttires)

struct BurnDriver BurnSpecBcquesttires = {
	"spec_bcquesttires", NULL, "spec_spec128", NULL, "1985",
	"BC's Quest for Tires\0", "Press P to game inputs menu", "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecBcquesttiresRomInfo, SpecBcquesttiresRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blob the Cop

static struct BurnRomInfo SpecBlobthecopRomDesc[] = {
	{ "Blob the Cop (1989)(Players Software).tap", 42196, 0x09b3e306, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBlobthecop, SpecBlobthecop, Spec128)
STD_ROM_FN(SpecBlobthecop)

struct BurnDriver BurnSpecBlobthecop = {
	"spec_blobthecop", NULL, "spec_spec128", NULL, "1989",
	"Blob the Cop\0", NULL, "Players Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBlobthecopRomInfo, SpecBlobthecopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bubbler

static struct BurnRomInfo SpecBubblerRomDesc[] = {
	{ "Bubbler (1987)(Ultimate Play The Game).tap", 41660, 0x8e3e4b5a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBubbler, SpecBubbler, Spec128)
STD_ROM_FN(SpecBubbler)

struct BurnDriver BurnSpecBubbler = {
	"spec_bubbler", NULL, "spec_spec128", NULL, "1987",
	"Bubbler\0", NULL, "Ultimate Play The Game", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBubblerRomInfo, SpecBubblerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Buggy Ranger

static struct BurnRomInfo SpecBuggyrangerRomDesc[] = {
	{ "Buggy Ranger (1990)(Dinamic).tap", 50964, 0xc7f9d095, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBuggyranger, SpecBuggyranger, Spec128)
STD_ROM_FN(SpecBuggyranger)

struct BurnDriver BurnSpecBuggyranger = {
	"spec_buggyranger", NULL, "spec_spec128", NULL, "1990",
	"Buggy Ranger\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBuggyrangerRomInfo, SpecBuggyrangerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chain Reaction

static struct BurnRomInfo SpecChainreactionRomDesc[] = {
	{ "Chain Reaction (1987)(Durell).tap", 51862, 0x42c4cd17, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecChainreaction, SpecChainreaction, Spec128)
STD_ROM_FN(SpecChainreaction)

struct BurnDriver BurnSpecChainreaction = {
	"spec_chainreaction", NULL, "spec_spec128", NULL, "1987",
	"Chain Reaction\0", NULL, "Durell", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecChainreactionRomInfo, SpecChainreactionRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chickin Chase (Il Galletto)

static struct BurnRomInfo SpecChickinchaseRomDesc[] = {
	{ "Chickin Chase (1985)(Firebird).tap", 50446, 0x5762f239, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecChickinchase, SpecChickinchase, Spectrum)
STD_ROM_FN(SpecChickinchase)

struct BurnDriver BurnSpecChickinchase = {
	"spec_chickinchase", NULL, "spec_spectrum", NULL, "1985",
	"Chickin Chase (Il Galletto)\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecChickinchaseRomInfo, SpecChickinchaseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Comando Quatro

static struct BurnRomInfo SpecComandoquatroRomDesc[] = {
	{ "Comando Quatro (1989)(Zigurat).tap", 49485, 0x11a8ebc0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecComandoquatro, SpecComandoquatro, Spec128)
STD_ROM_FN(SpecComandoquatro)

struct BurnDriver BurnSpecComandoquatro = {
	"spec_comandoquatro", NULL, "spec_spec128", NULL, "1989",
	"Comando Quatro\0", NULL, "Zigurat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecComandoquatroRomInfo, SpecComandoquatroRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cosmic Wartoad

static struct BurnRomInfo SpecCosmicwartoadRomDesc[] = {
	{ "Cosmic Wartoad (1985)(Ocean).tap", 45221, 0x81e8ae65, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCosmicwartoad, SpecCosmicwartoad, Spec128)
STD_ROM_FN(SpecCosmicwartoad)

struct BurnDriver BurnSpecCosmicwartoad = {
	"spec_cosmicwartoad", NULL, "spec_spec128", NULL, "1985",
	"Cosmic Wartoad\0", NULL, "Ocean", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCosmicwartoadRomInfo, SpecCosmicwartoadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dark Star

static struct BurnRomInfo SpecDarkstarRomDesc[] = {
	{ "Dark Star (1984)(Design Design).tap", 45980, 0x38a11d60, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDarkstar, SpecDarkstar, Spec128)
STD_ROM_FN(SpecDarkstar)

struct BurnDriver BurnSpecDarkstar = {
	"spec_darkstar", NULL, "spec_spec128", NULL, "1984",
	"Dark Star\0", NULL, "Design Design", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDarkstarRomInfo, SpecDarkstarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dea Tenebrarum

static struct BurnRomInfo SpecDeatenebrarumRomDesc[] = {
	{ "Dea Tenebrarum (1988)(System 4).tap", 48584, 0xef594c3b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDeatenebrarum, SpecDeatenebrarum, Spec128)
STD_ROM_FN(SpecDeatenebrarum)

struct BurnDriver BurnSpecDeatenebrarum = {
	"spec_deatenebrarum", NULL, "spec_spec128", NULL, "1988",
	"Dea Tenebrarum\0", NULL, "System 4", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDeatenebrarumRomInfo, SpecDeatenebrarumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Denizen

static struct BurnRomInfo SpecDenizenRomDesc[] = {
	{ "Denizen (1988)(Players).tap", 112972, 0x6ae6dff4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDenizen, SpecDenizen, Spec128)
STD_ROM_FN(SpecDenizen)

struct BurnDriver BurnSpecDenizen = {
	"spec_denizen", NULL, "spec_spec128", NULL, "1988",
	"Denizen\0", NULL, "Players", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDenizenRomInfo, SpecDenizenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Disco Dan

static struct BurnRomInfo SpecDiscodanRomDesc[] = {
	{ "Disco Dan (1984)(Gem Software).tap", 19931, 0x8ba92db3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDiscodan, SpecDiscodan, Spec128)
STD_ROM_FN(SpecDiscodan)

struct BurnDriver BurnSpecDiscodan = {
	"spec_discodan", NULL, "spec_spec128", NULL, "1984",
	"Disco Dan\0", NULL, "Gem Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDiscodanRomInfo, SpecDiscodanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Duet

static struct BurnRomInfo SpecDuetRomDesc[] = {
	{ "Duet (1987)(Elite).tap", 41164, 0x80308e30, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDuet, SpecDuet, Spec128)
STD_ROM_FN(SpecDuet)

struct BurnDriver BurnSpecDuet = {
	"spec_duet", NULL, "spec_spec128", NULL, "1987",
	"Duet\0", NULL, "Elite", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDuetRomInfo, SpecDuetRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Eliminator

static struct BurnRomInfo SpecEliminatorRomDesc[] = {
	{ "Eliminator (1988)(Hewson).tap", 44964, 0x4e5643c0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEliminator, SpecEliminator, Spec128)
STD_ROM_FN(SpecEliminator)

struct BurnDriver BurnSpecEliminator = {
	"spec_eliminator", NULL, "spec_spec128", NULL, "1988",
	"Eliminator\0", NULL, "Hewson", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecEliminatorRomInfo, SpecEliminatorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fearless Frank

static struct BurnRomInfo SpecFearlessfrankRomDesc[] = {
	{ "Fearless Frank (1984)(Visions).tap", 40614, 0x573889ab, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFearlessfrank, SpecFearlessfrank, Spec128)
STD_ROM_FN(SpecFearlessfrank)

struct BurnDriver BurnSpecFearlessfrank = {
	"spec_fearlessfrank", NULL, "spec_spec128", NULL, "1984",
	"Fearless Frank\0", NULL, "Visions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFearlessfrankRomInfo, SpecFearlessfrankRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Foxx Fights Back

static struct BurnRomInfo SpecFoxxfightsbackRomDesc[] = {
	{ "Foxx Fights Back (1988)(Image Works).tap", 47395, 0x7c9ae4b5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFoxxfightsback, SpecFoxxfightsback, Spec128)
STD_ROM_FN(SpecFoxxfightsback)

struct BurnDriver BurnSpecFoxxfightsback = {
	"spec_foxxfightsback", NULL, "spec_spec128", NULL, "1988",
	"Foxx Fights Back\0", NULL, "Image Works", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFoxxfightsbackRomInfo, SpecFoxxfightsbackRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Blade

static struct BurnRomInfo SpecStarbladeRomDesc[] = {
	{ "Star Blade (1995)(Barry Addams).z80", 38880, 0x96e096c6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarblade, SpecStarblade, Spectrum)
STD_ROM_FN(SpecStarblade)

struct BurnDriver BurnSpecStarblade = {
	"spec_starblade", NULL, "spec_spectrum", NULL, "1995",
	"Star Blade\0", NULL, "Barry Addams", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStarbladeRomInfo, SpecStarbladeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// UVE Atac

static struct BurnRomInfo SpecUveatacRomDesc[] = {
	{ "UVE Atac (1985)(MicroHobby).tap", 17979, 0x78f60aea, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecUveatac, SpecUveatac, Spec128)
STD_ROM_FN(SpecUveatac)

struct BurnDriver BurnSpecUveatac = {
	"spec_uveatac", NULL, "spec_spec128", NULL, "1985",
	"UVE Atac\0", NULL, "MicroHobby", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecUveatacRomInfo, SpecUveatacRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Zub (128K)

static struct BurnRomInfo SpecZubRomDesc[] = {
	{ "Zub (1986)(Mastertronic Added Dimension).tap", 62034, 0xe48404af, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecZub, SpecZub, Spec128)
STD_ROM_FN(SpecZub)

struct BurnDriver BurnSpecZub = {
	"spec_zub", NULL, "spec_spec128", NULL, "1986",
	"Zub (128K)\0", NULL, "Mastertronic Added Dimension", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecZubRomInfo, SpecZubRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gunhead

static struct BurnRomInfo SpecGunheadRomDesc[] = {
	{ "Gunhead (1990)(Hellenic).tap", 47800, 0xd1c99fb1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGunhead, SpecGunhead, Spec128)
STD_ROM_FN(SpecGunhead)

struct BurnDriver BurnSpecGunhead = {
	"spec_gunhead", NULL, "spec_spec128", NULL, "1990",
	"Gunhead\0", NULL, "Hellenic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecGunheadRomInfo, SpecGunheadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hunter

static struct BurnRomInfo SpecHunterRomDesc[] = {
	{ "Hunter (1991)(Stuffduck).tap", 47590, 0xac3c997b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHunter, SpecHunter, Spec128)
STD_ROM_FN(SpecHunter)

struct BurnDriver BurnSpecHunter = {
	"spec_hunter", NULL, "spec_spec128", NULL, "1991",
	"Hunter\0", NULL, "Stuffduck", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHunterRomInfo, SpecHunterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jackson City

static struct BurnRomInfo SpecJacksoncityRomDesc[] = {
	{ "Jackson City (1990)(Diabolic).tap", 42878, 0x320d1aec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJacksoncity, SpecJacksoncity, Spectrum)
STD_ROM_FN(SpecJacksoncity)

struct BurnDriver BurnSpecJacksoncity = {
	"spec_jacksoncity", NULL, "spec_spectrum", NULL, "1990",
	"Jackson City\0", NULL, "Diabolic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecJacksoncityRomInfo, SpecJacksoncityRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Laser Zone

static struct BurnRomInfo SpecLaserzoneRomDesc[] = {
	{ "Laser Zone (1983)(Quicksilva).tap", 17800, 0x701e7685, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLaserzone, SpecLaserzone, Spectrum)
STD_ROM_FN(SpecLaserzone)

struct BurnDriver BurnSpecLaserzone = {
	"spec_laserzone", NULL, "spec_spectrum", NULL, "1983",
	"Laser Zone\0", NULL, "Quicksilva", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecLaserzoneRomInfo, SpecLaserzoneRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mask

static struct BurnRomInfo SpecMaskRomDesc[] = {
	{ "Mask (1987)(Gremlin).z80", 92283, 0xcdd8c4b1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMask, SpecMask, Spec128)
STD_ROM_FN(SpecMask)

struct BurnDriver BurnSpecMask = {
	"spec_mask", NULL, "spec_spec128", NULL, "1987",
	"Mask\0", NULL, "Gremlin", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMaskRomInfo, SpecMaskRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Master Blaster

static struct BurnRomInfo SpecMasterblasterRomDesc[] = {
	{ "Master Blaster (1990)(Cybadyne).tap", 49304, 0x83f104ce, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMasterblaster, SpecMasterblaster, Spec128)
STD_ROM_FN(SpecMasterblaster)

struct BurnDriver BurnSpecMasterblaster = {
	"spec_masterblaster", NULL, "spec_spec128", NULL, "1990",
	"Master Blaster\0", NULL, "Cybadyne", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMasterblasterRomInfo, SpecMasterblasterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mean Streak

static struct BurnRomInfo SpecMeanstreakRomDesc[] = {
	{ "Mean Streak (1987)(Mirrorsoft).tap", 66912, 0xeaa7cbc4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMeanstreak, SpecMeanstreak, Spec128)
STD_ROM_FN(SpecMeanstreak)

struct BurnDriver BurnSpecMeanstreak = {
	"spec_meanstreak", NULL, "spec_spec128", NULL, "1987",
	"Mean Streak\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMeanstreakRomInfo, SpecMeanstreakRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mega Apocalypse

static struct BurnRomInfo SpecMegaapocalypseRomDesc[] = {
	{ "Mega Apocalypse (1988)(Martech).tap", 45336, 0x989130c0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMegaapocalypse, SpecMegaapocalypse, Spec128)
STD_ROM_FN(SpecMegaapocalypse)

struct BurnDriver BurnSpecMegaapocalypse = {
	"spec_megaapocalypse", NULL, "spec_spec128", NULL, "1988",
	"Mega Apocalypse\0", NULL, "Martech", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMegaapocalypseRomInfo, SpecMegaapocalypseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Megaphoenix

static struct BurnRomInfo SpecMegaphoenixRomDesc[] = {
	{ "Megaphoenix (1991)(Dinamic).tap", 43229, 0x81937ff0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMegaphoenix, SpecMegaphoenix, Spec128)
STD_ROM_FN(SpecMegaphoenix)

struct BurnDriver BurnSpecMegaphoenix = {
	"spec_megaphoenix", NULL, "spec_spec128", NULL, "1991",
	"Megaphoenix\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMegaphoenixRomInfo, SpecMegaphoenixRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moon Strike

static struct BurnRomInfo SpecMoonstrikeRomDesc[] = {
	{ "Moon Strike (1987)(Mirrorsoft).tap", 49633, 0xbd0e5cbd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMoonstrike, SpecMoonstrike, Spec128)
STD_ROM_FN(SpecMoonstrike)

struct BurnDriver BurnSpecMoonstrike = {
	"spec_moonstrike", NULL, "spec_spec128", NULL, "1987",
	"Moon Strike\0", NULL, "Mirrorsoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMoonstrikeRomInfo, SpecMoonstrikeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Paratroopers

static struct BurnRomInfo SpecParatroopersRomDesc[] = {
	{ "Paratroopers (1983)(Rabbit).tap", 21732, 0xc19b3a5d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecParatroopers, SpecParatroopers, Spectrum)
STD_ROM_FN(SpecParatroopers)

struct BurnDriver BurnSpecParatroopers = {
	"spec_paratroopers", NULL, "spec_spectrum", NULL, "1983",
	"Paratroopers\0", NULL, "Rabbit", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecParatroopersRomInfo, SpecParatroopersRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phileas Fogg's Balloon Battles

static struct BurnRomInfo SpecPhileasfoggRomDesc[] = {
	{ "Phileas Fogg's Balloon Battles (1991)(Zeppelin).tap", 48594, 0xc311768c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPhileasfogg, SpecPhileasfogg, Spec128)
STD_ROM_FN(SpecPhileasfogg)

struct BurnDriver BurnSpecPhileasfogg = {
	"spec_phileasfogg", NULL, "spec_spec128", NULL, "1991",
	"Phileas Fogg's Balloon Battles\0", NULL, "Zeppelin", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPhileasfoggRomInfo, SpecPhileasfoggRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Quick Draw McGraw (128K)

static struct BurnRomInfo SpecQuickdrawRomDesc[] = {
	{ "Quick Draw McGraw (1991)(Hi-Tec).tap", 53434, 0x435650ba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecQuickdraw, SpecQuickdraw, Spec128)
STD_ROM_FN(SpecQuickdraw)

struct BurnDriver BurnSpecQuickdraw = {
	"spec_quickdraw", NULL, "spec_spec128", NULL, "1991",
	"Quick Draw McGraw (128K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecQuickdrawRomInfo, SpecQuickdrawRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Seahawk

static struct BurnRomInfo SpecSeahawkRomDesc[] = {
	{ "Seahawk (1990)(Atlantis Software).tap", 48448, 0x32c8705a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSeahawk, SpecSeahawk, Spec128)
STD_ROM_FN(SpecSeahawk)

struct BurnDriver BurnSpecSeahawk = {
	"spec_seahawk", NULL, "spec_spec128", NULL, "1990",
	"Seahawk\0", NULL, "Atlantis Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSeahawkRomInfo, SpecSeahawkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shadow Skimmer

static struct BurnRomInfo SpecShadowskimmerRomDesc[] = {
	{ "Shadow Skimmer (1987)(The Edge).z80", 41483, 0xc7d0a073, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecShadowskimmer, SpecShadowskimmer, Spectrum)
STD_ROM_FN(SpecShadowskimmer)

struct BurnDriver BurnSpecShadowskimmer = {
	"spec_shadowskimmer", NULL, "spec_spectrum", NULL, "1987",
	"Shadow Skimmer\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecShadowskimmerRomInfo, SpecShadowskimmerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shark

static struct BurnRomInfo SpecSharkRomDesc[] = {
	{ "Shark (1989)(Players Premier).tap", 101962, 0x014ce429, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecShark, SpecShark, Spec128)
STD_ROM_FN(SpecShark)

struct BurnDriver BurnSpecShark = {
	"spec_shark", NULL, "spec_spec128", NULL, "1989",
	"Shark\0", NULL, "Players Premier", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSharkRomInfo, SpecSharkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Firebirds

static struct BurnRomInfo SpecStarfirebirdsRomDesc[] = {
	{ "Star Firebirds (1985)(Firebird).z80", 34053, 0xe38dec78, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarfirebirds, SpecStarfirebirds, Spectrum)
STD_ROM_FN(SpecStarfirebirds)

struct BurnDriver BurnSpecStarfirebirds = {
	"spec_starfirebirds", NULL, "spec_spectrum", NULL, "1985",
	"Star Firebirds\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStarfirebirdsRomInfo, SpecStarfirebirdsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tomcat

static struct BurnRomInfo SpecTomcatRomDesc[] = {
	{ "Tomcat (1989)(Players).tap", 41255, 0xef75de5c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTomcat, SpecTomcat, Spec128)
STD_ROM_FN(SpecTomcat)

struct BurnDriver BurnSpecTomcat = {
	"spec_tomcat", NULL, "spec_spec128", NULL, "1989",
	"Tomcat\0", NULL, "Players", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTomcatRomInfo, SpecTomcatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// War Machine

static struct BurnRomInfo SpecWarmachineRomDesc[] = {
	{ "War Machine (1989)(Players Premier).tap", 54991, 0x7ac04bb1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWarmachine, SpecWarmachine, Spec128)
STD_ROM_FN(SpecWarmachine)

struct BurnDriver BurnSpecWarmachine = {
	"spec_warmachine", NULL, "spec_spec128", NULL, "1989",
	"War Machine\0", NULL, "Players Premier", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWarmachineRomInfo, SpecWarmachineRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Who Dares Wins II

static struct BurnRomInfo SpecWhodareswinsiiRomDesc[] = {
	{ "Who Dares Wins II (1986)(Alligata).tap", 48631, 0x02dc6931, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWhodareswinsii, SpecWhodareswinsii, Spec128)
STD_ROM_FN(SpecWhodareswinsii)

struct BurnDriver BurnSpecWhodareswinsii = {
	"spec_whodareswinsii", NULL, "spec_spec128", NULL, "1986",
	"Who Dares Wins II\0", NULL, "Alligata", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWhodareswinsiiRomInfo, SpecWhodareswinsiiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 007 - Live and Let Die (128K)

static struct BurnRomInfo Spec007liveandletdieRomDesc[] = {
	{ "007 - Live and Let Die (1988)(Domark).tap", 62379, 0x5f21da6b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec007liveandletdie, Spec007liveandletdie, Spec128)
STD_ROM_FN(Spec007liveandletdie)

struct BurnDriver BurnSpec007liveandletdie = {
	"spec_007liveandletdie", NULL, "spec_spec128", NULL, "1988",
	"007 - Live and Let Die (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, Spec007liveandletdieRomInfo, Spec007liveandletdieRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 007 - The Spy Who Loved Me (128K)

static struct BurnRomInfo Spec007spywholovedmeRomDesc[] = {
	{ "007 - The Spy Who Loved Me (1990)(Domark).tap", 129851, 0x629c140c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec007spywholovedme, Spec007spywholovedme, Spec128)
STD_ROM_FN(Spec007spywholovedme)

struct BurnDriver BurnSpec007spywholovedme = {
	"spec_007spywholovedme", NULL, "spec_spec128", NULL, "1990",
	"007 - The Spy Who Loved Me (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING | GBF_VERSHOOT, 0,
	SpectrumGetZipName, Spec007spywholovedmeRomInfo, Spec007spywholovedmeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 2088

static struct BurnRomInfo Spec2088RomDesc[] = {
	{ "2088 (1988)(Zeppelin Games).tap", 28306, 0xdecaeb6e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec2088, Spec2088, Spec128)
STD_ROM_FN(Spec2088)

struct BurnDriver BurnSpec2088 = {
	"spec_2088", NULL, "spec_spec128", NULL, "1988",
	"2088\0", NULL, "Zeppelin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec2088RomInfo, Spec2088RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 3D Starfighter

static struct BurnRomInfo Spec3dstarfighterRomDesc[] = {
	{ "3D Starfighter (1988)(Codemasters).tap", 48891, 0x4f3f2fbe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec3dstarfighter, Spec3dstarfighter, Spectrum)
STD_ROM_FN(Spec3dstarfighter)

struct BurnDriver BurnSpec3dstarfighter = {
	"spec_3dstarfighter", NULL, "spec_spectrum", NULL, "1988",
	"3D Starfighter\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec3dstarfighterRomInfo, Spec3dstarfighterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 3D-Tetris

static struct BurnRomInfo Spec3dtetrisRomDesc[] = {
	{ "3D-Tetris (1989)(Antic).z80", 19178, 0xe46b5e70, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec3dtetris, Spec3dtetris, Spectrum)
STD_ROM_FN(Spec3dtetris)

struct BurnDriver BurnSpec3dtetris = {
	"spec_3dtetris", NULL, "spec_spectrum", NULL, "1989",
	"3D-Tetris\0", NULL, "Antic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec3dtetrisRomInfo, Spec3dtetrisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// 5 in 1 Tetris

static struct BurnRomInfo Spec5in1tetrisRomDesc[] = {
	{ "5 in 1 Tetris (1995)(Mortal Kombat).z80", 28526, 0x996128b2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spec5in1tetris, Spec5in1tetris, Spec128)
STD_ROM_FN(Spec5in1tetris)

struct BurnDriver BurnSpec5in1tetris = {
	"spec_5in1tetris", NULL, "spec_spec128", NULL, "1995",
	"5 in 1 Tetris\0", NULL, "Mortal Kombat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, Spec5in1tetrisRomInfo, Spec5in1tetrisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ammytris

static struct BurnRomInfo SpecAmmytrisRomDesc[] = {
	{ "Ammytris (1995)(Navigator).tap", 19901, 0x24de1080, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAmmytris, SpecAmmytris, Spec128)
STD_ROM_FN(SpecAmmytris)

struct BurnDriver BurnSpecAmmytris = {
	"spec_ammytris", NULL, "spec_spec128", NULL, "1995",
	"Ammytris\0", NULL, "Navigator", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecAmmytrisRomInfo, SpecAmmytrisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dragon Spirit

static struct BurnRomInfo SpecDragonspiritRomDesc[] = {
	{ "Dragon Spirit (1989)(Domark).tap", 164427, 0xadc4c7cf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDragonspirit, SpecDragonspirit, Spec128)
STD_ROM_FN(SpecDragonspirit)

struct BurnDriver BurnSpecDragonspirit = {
	"spec_dragonspirit", NULL, "spec_spec128", NULL, "1989",
	"Dragon Spirit\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDragonspiritRomInfo, SpecDragonspiritRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Little Puff in Dragonland

static struct BurnRomInfo SpecLittlepuffRomDesc[] = {
	{ "Little Puff in Dragonland (1990)(Codemasters).tap", 42827, 0x30a03963, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLittlepuff, SpecLittlepuff, Spec128)
STD_ROM_FN(SpecLittlepuff)

struct BurnDriver BurnSpecLittlepuff = {
	"spec_littlepuff", NULL, "spec_spec128", NULL, "1990",
	"Little Puff in Dragonland\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecLittlepuffRomInfo, SpecLittlepuffRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Miami Chase

static struct BurnRomInfo SpecMiamichaseRomDesc[] = {
	{ "Miami Chase (1991)(Codemasters).tap", 48671, 0x24cb897c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMiamichase, SpecMiamichase, Spec128)
STD_ROM_FN(SpecMiamichase)

struct BurnDriver BurnSpecMiamichase = {
	"spec_miamichase", NULL, "spec_spec128", NULL, "1991",
	"Miami Chase\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMiamichaseRomInfo, SpecMiamichaseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mig 29 Soviet Fighter

static struct BurnRomInfo SpecMig29sovfighterRomDesc[] = {
	{ "Mig 29 Soviet Fighter (1989)(Codemasters).tap", 49442, 0x93a448fd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMig29sovfighter, SpecMig29sovfighter, Spec128)
STD_ROM_FN(SpecMig29sovfighter)

struct BurnDriver BurnSpecMig29sovfighter = {
	"spec_mig29sovfighter", NULL, "spec_spec128", NULL, "1989",
	"Mig 29 Soviet Fighter\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMig29sovfighterRomInfo, SpecMig29sovfighterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mission Jupiter

static struct BurnRomInfo SpecMissionjupiterRomDesc[] = {
	{ "Mission Jupiter (1987)(Codemasters).tap", 45152, 0xf136a5b9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMissionjupiter, SpecMissionjupiter, Spec128)
STD_ROM_FN(SpecMissionjupiter)

struct BurnDriver BurnSpecMissionjupiter = {
	"spec_missionjupiter", NULL, "spec_spec128", NULL, "1987",
	"Mission Jupiter\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMissionjupiterRomInfo, SpecMissionjupiterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Monte Carlo Casino

static struct BurnRomInfo SpecMontecarlocasinoRomDesc[] = {
	{ "Monte Carlo Casino (1989)(Codemasters).tap", 41821, 0x8d459dc5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMontecarlocasino, SpecMontecarlocasino, Spec128)
STD_ROM_FN(SpecMontecarlocasino)

struct BurnDriver BurnSpecMontecarlocasino = {
	"spec_montecarlocasino", NULL, "spec_spec128", NULL, "1989",
	"Monte Carlo Casino\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMontecarlocasinoRomInfo, SpecMontecarlocasinoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moto Cross Simulator

static struct BurnRomInfo SpecMotocrosssimRomDesc[] = {
	{ "Moto Cross Simulator (1989)(Codemasters).tap", 51225, 0x7abd5f87, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMotocrosssim, SpecMotocrosssim, Spec128)
STD_ROM_FN(SpecMotocrosssim)

struct BurnDriver BurnSpecMotocrosssim = {
	"spec_motocrosssim", NULL, "spec_spec128", NULL, "1989",
	"Moto Cross Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMotocrosssimRomInfo, SpecMotocrosssimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mountain Bike Simulator

static struct BurnRomInfo SpecMountainbikesimRomDesc[] = {
	{ "Mountain Bike Simulator (1991)(Codemasters).tap", 53006, 0xd2738989, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMountainbikesim, SpecMountainbikesim, Spec128)
STD_ROM_FN(SpecMountainbikesim)

struct BurnDriver BurnSpecMountainbikesim = {
	"spec_mountainbikesim", NULL, "spec_spec128", NULL, "1991",
	"Mountain Bike Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMountainbikesimRomInfo, SpecMountainbikesimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Murray Mouse Super Cop

static struct BurnRomInfo SpecMurraymousesupercopRomDesc[] = {
	{ "Murray Mouse Super Cop (1992)(Codemasters).tap", 32045, 0x67637ddf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMurraymousesupercop, SpecMurraymousesupercop, Spectrum)
STD_ROM_FN(SpecMurraymousesupercop)

struct BurnDriver BurnSpecMurraymousesupercop = {
	"spec_murraymousesupercop", NULL, "spec_spectrum", NULL, "1992",
	"Murray Mouse Super Cop\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMurraymousesupercopRomInfo, SpecMurraymousesupercopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Necris Dome

static struct BurnRomInfo SpecNecrisdomeRomDesc[] = {
	{ "Necris Dome (1987)(Codemasters).tap", 48602, 0x731d02fb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNecrisdome, SpecNecrisdome, Spec128)
STD_ROM_FN(SpecNecrisdome)

struct BurnDriver BurnSpecNecrisdome = {
	"spec_necrisdome", NULL, "spec_spec128", NULL, "1987",
	"Necris Dome\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecNecrisdomeRomInfo, SpecNecrisdomeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninja Massacre (128K)

static struct BurnRomInfo SpecNinjamassacreRomDesc[] = {
	{ "Ninja Massacre (1989)(Codemasters).tap", 59148, 0x2b28000a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNinjamassacre, SpecNinjamassacre, Spec128)
STD_ROM_FN(SpecNinjamassacre)

struct BurnDriver BurnSpecNinjamassacre = {
	"spec_ninjamassacre", NULL, "spec_spec128", NULL, "1989",
	"Ninja Massacre (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_MAZE, 0,
	SpectrumGetZipName, SpecNinjamassacreRomInfo, SpecNinjamassacreRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Operation Gunship

static struct BurnRomInfo SpecOperationgunshipRomDesc[] = {
	{ "Operation Gunship (1989)(Codemasters).tap", 52455, 0xee1f687d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOperationgunship, SpecOperationgunship, Spec128)
STD_ROM_FN(SpecOperationgunship)

struct BurnDriver BurnSpecOperationgunship = {
	"spec_operationgunship", NULL, "spec_spec128", NULL, "1989",
	"Operation Gunship\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecOperationgunshipRomInfo, SpecOperationgunshipRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Paris To Dakar

static struct BurnRomInfo SpecParistodakarRomDesc[] = {
	{ "Paris To Dakar (1991)(Codemasters).tap", 73017, 0xcb4a5edc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecParistodakar, SpecParistodakar, Spec128)
STD_ROM_FN(SpecParistodakar)

struct BurnDriver BurnSpecParistodakar = {
	"spec_paristodakar", NULL, "spec_spec128", NULL, "1991",
	"Paris To Dakar\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecParistodakarRomInfo, SpecParistodakarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phantomas

static struct BurnRomInfo SpecPhantomasRomDesc[] = {
	{ "Phantomas (1986)(Dinamic).tap", 48127, 0x34c8919a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPhantomas, SpecPhantomas, Spectrum)
STD_ROM_FN(SpecPhantomas)

struct BurnDriver BurnSpecPhantomas = {
	"spec_phantomas", NULL, "spec_spectrum", NULL, "1986",
	"Phantomas\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPhantomasRomInfo, SpecPhantomasRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phantomas 2

static struct BurnRomInfo SpecPhantomas2RomDesc[] = {
	{ "Phantomas 2 (1986)(Dinamic).tap", 49300, 0x7e1fecc2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPhantomas2, SpecPhantomas2, Spec128)
STD_ROM_FN(SpecPhantomas2)

struct BurnDriver BurnSpecPhantomas2 = {
	"spec_phantomas2", NULL, "spec_spec128", NULL, "1986",
	"Phantomas 2\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPhantomas2RomInfo, SpecPhantomas2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Prince Clumsy

static struct BurnRomInfo SpecPrinceclumsyRomDesc[] = {
	{ "Prince Clumsy (1990)(Codemasters).z80", 36725, 0xc5ab98f3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPrinceclumsy, SpecPrinceclumsy, Spectrum)
STD_ROM_FN(SpecPrinceclumsy)

struct BurnDriver BurnSpecPrinceclumsy = {
	"spec_princeclumsy", NULL, "spec_spectrum", NULL, "1990",
	"Prince Clumsy\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPrinceclumsyRomInfo, SpecPrinceclumsyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Professional BMX Simulator - Expert

static struct BurnRomInfo SpecProbmxsimexpertRomDesc[] = {
	{ "Professional BMX Simulator - Expert (1988)(Codemasters).tap", 81052, 0x5f895fc1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProbmxsimexpert, SpecProbmxsimexpert, Spec128)
STD_ROM_FN(SpecProbmxsimexpert)

struct BurnDriver BurnSpecProbmxsimexpert = {
	"spec_probmxsimexpert", NULL, "spec_spec128", NULL, "1988",
	"Professional BMX Simulator - Expert\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProbmxsimexpertRomInfo, SpecProbmxsimexpertRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Professional BMX Simulator - Standard

static struct BurnRomInfo SpecProbmxsimstandardRomDesc[] = {
	{ "Professional BMX Simulator - Standard (1988)(Codemasters).tap", 81052, 0x7468e20b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProbmxsimstandard, SpecProbmxsimstandard, Spec128)
STD_ROM_FN(SpecProbmxsimstandard)

struct BurnDriver BurnSpecProbmxsimstandard = {
	"spec_probmxsimstandard", NULL, "spec_spec128", NULL, "1988",
	"Professional BMX Simulator - Standard\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProbmxsimstandardRomInfo, SpecProbmxsimstandardRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pro Golf Simulator

static struct BurnRomInfo SpecProgolfsimRomDesc[] = {
	{ "Pro Golf Simulator (1990)(Codemasters).tap", 48603, 0x6e5bed28, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProgolfsim, SpecProgolfsim, Spec128)
STD_ROM_FN(SpecProgolfsim)

struct BurnDriver BurnSpecProgolfsim = {
	"spec_progolfsim", NULL, "spec_spec128", NULL, "1990",
	"Pro Golf Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProgolfsimRomInfo, SpecProgolfsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pro PowerBoat Simulator

static struct BurnRomInfo SpecPropowerboatsimRomDesc[] = {
	{ "Pro PowerBoat Simulator (1989)(Codemasters).tap", 49394, 0x5627c3f7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPropowerboatsim, SpecPropowerboatsim, Spec128)
STD_ROM_FN(SpecPropowerboatsim)

struct BurnDriver BurnSpecPropowerboatsim = {
	"spec_propowerboatsim", NULL, "spec_spec128", NULL, "1989",
	"Pro PowerBoat Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPropowerboatsimRomInfo, SpecPropowerboatsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pro Skateboard Simulator

static struct BurnRomInfo SpecProskateboardsimRomDesc[] = {
	{ "Pro Skateboard Simulator (1989)(Codemasters).z80", 43055, 0x128b51fb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProskateboardsim, SpecProskateboardsim, Spec128)
STD_ROM_FN(SpecProskateboardsim)

struct BurnDriver BurnSpecProskateboardsim = {
	"spec_proskateboardsim", NULL, "spec_spec128", NULL, "1989",
	"Pro Skateboard Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProskateboardsimRomInfo, SpecProskateboardsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Professional Ski Simulator

static struct BurnRomInfo SpecProskisimRomDesc[] = {
	{ "Professional Ski Simulator (1987)(Codemasters).tap", 48913, 0xce33d499, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProskisim, SpecProskisim, Spectrum)
STD_ROM_FN(SpecProskisim)

struct BurnDriver BurnSpecProskisim = {
	"spec_proskisim", NULL, "spec_spectrum", NULL, "1987",
	"Professional Ski Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProskisimRomInfo, SpecProskisimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Professional Snooker Simulator

static struct BurnRomInfo SpecProsnookersimRomDesc[] = {
	{ "Professional Snooker Simulator (1987)(Codemasters).z80", 22455, 0x1265eecd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProsnookersim, SpecProsnookersim, Spectrum)
STD_ROM_FN(SpecProsnookersim)

struct BurnDriver BurnSpecProsnookersim = {
	"spec_prosnookersim", NULL, "spec_spectrum", NULL, "1987",
	"Professional Snooker Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProsnookersimRomInfo, SpecProsnookersimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pro Tennis Simulator

static struct BurnRomInfo SpecProtennissimRomDesc[] = {
	{ "Pro Tennis Simulator (1990)(Codemasters).z80", 27039, 0x9e79b85c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecProtennissim, SpecProtennissim, Spec128)
STD_ROM_FN(SpecProtennissim)

struct BurnDriver BurnSpecProtennissim = {
	"spec_protennissim", NULL, "spec_spec128", NULL, "1990",
	"Pro Tennis Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecProtennissimRomInfo, SpecProtennissimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pub Trivia

static struct BurnRomInfo SpecPubtriviaRomDesc[] = {
	{ "Pub Trivia (1989)(Codemasters).z80", 100743, 0xbafbf528, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPubtrivia, SpecPubtrivia, Spec128)
STD_ROM_FN(SpecPubtrivia)

struct BurnDriver BurnSpecPubtrivia = {
	"spec_pubtrivia", NULL, "spec_spec128", NULL, "1989",
	"Pub Trivia\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPubtriviaRomInfo, SpecPubtriviaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Race Against Time, The

static struct BurnRomInfo SpecRaceagainsttimeRomDesc[] = {
	{ "Race Against Time, The (1988)(Codemasters).z80", 44167, 0xfe86ca68, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRaceagainsttime, SpecRaceagainsttime, Spectrum)
STD_ROM_FN(SpecRaceagainsttime)

struct BurnDriver BurnSpecRaceagainsttime = {
	"spec_raceagainsttime", NULL, "spec_spectrum", NULL, "1988",
	"Race Against Time, The\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRaceagainsttimeRomInfo, SpecRaceagainsttimeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rally Cross Simulator

static struct BurnRomInfo SpecRallycrosssimRomDesc[] = {
	{ "Rally Cross Simulator (1990)(Codemasters).tap", 111041, 0xa3f861ee, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRallycrosssim, SpecRallycrosssim, Spec128)
STD_ROM_FN(SpecRallycrosssim)

struct BurnDriver BurnSpecRallycrosssim = {
	"spec_rallycrosssim", NULL, "spec_spec128", NULL, "1990",
	"Rally Cross Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRallycrosssimRomInfo, SpecRallycrosssimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Robin Hood - Legend Quest

static struct BurnRomInfo SpecRobinhoodlegendquestRomDesc[] = {
	{ "Robin Hood - Legend Quest (1993)(Codemasters).tap", 79970, 0x548b209e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRobinhoodlegendquest, SpecRobinhoodlegendquest, Spec128)
STD_ROM_FN(SpecRobinhoodlegendquest)

struct BurnDriver BurnSpecRobinhoodlegendquest = {
	"spec_robinhoodlegendquest", NULL, "spec_spec128", NULL, "1993",
	"Robin Hood - Legend Quest\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRobinhoodlegendquestRomInfo, SpecRobinhoodlegendquestRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rock Star Goes Bizarre

static struct BurnRomInfo SpecRockstargoesbizarreRomDesc[] = {
	{ "Rock Star Goes Bizarre (1989)(Codemasters).tap", 68929, 0x4dc0ff5a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRockstargoesbizarre, SpecRockstargoesbizarre, Spec128)
STD_ROM_FN(SpecRockstargoesbizarre)

struct BurnDriver BurnSpecRockstargoesbizarre = {
	"spec_rockstargoesbizarre", NULL, "spec_spec128", NULL, "1989",
	"Rock Star Goes Bizarre\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRockstargoesbizarreRomInfo, SpecRockstargoesbizarreRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rogue Trooper

static struct BurnRomInfo SpecRoguetrooperRomDesc[] = {
	{ "Rogue Trooper (1986)(Piranha).tap", 55832, 0x6e1e885e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRoguetrooper, SpecRoguetrooper, Spectrum)
STD_ROM_FN(SpecRoguetrooper)

struct BurnDriver BurnSpecRoguetrooper = {
	"spec_roguetrooper", NULL, "spec_spectrum", NULL, "1986",
	"Rogue Trooper\0", NULL, "Piranha", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRoguetrooperRomInfo, SpecRoguetrooperRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.A.S. Combat Simulator

static struct BurnRomInfo SpecSascombatsimRomDesc[] = {
	{ "S.A.S. Combat Simulator (1988)(Codemasters).tap", 42914, 0x6324df81, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSascombatsim, SpecSascombatsim, Spectrum)
STD_ROM_FN(SpecSascombatsim)

struct BurnDriver BurnSpecSascombatsim = {
	"spec_sascombatsim", NULL, "spec_spectrum", NULL, "1988",
	"S.A.S. Combat Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSascombatsimRomInfo, SpecSascombatsimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.A.S. Operation Thunderflash

static struct BurnRomInfo SpecSasoperationthunderflashRomDesc[] = {
	{ "S.A.S. Operation Thunderflash (1986)(Sparklers).tap", 48345, 0x7013a78b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSasoperationthunderflash, SpecSasoperationthunderflash, Spectrum)
STD_ROM_FN(SpecSasoperationthunderflash)

struct BurnDriver BurnSpecSasoperationthunderflash = {
	"spec_sasoperationthunderflash", NULL, "spec_spectrum", NULL, "1986",
	"S.A.S. Operation Thunderflash\0", NULL, "Sparklers", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSasoperationthunderflashRomInfo, SpecSasoperationthunderflashRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Scooby and Scrappy-Doo (48K)

static struct BurnRomInfo SpecSsdooRomDesc[] = {
	{ "Scooby and Scrappy-Doo (1991)(Hi-Tec).tap", 47868, 0x5e04acd0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSsdoo, SpecSsdoo, Spectrum)
STD_ROM_FN(SpecSsdoo)

struct BurnDriver BurnSpecSsdoo = {
	"spec_ssdoo", NULL, "spec_spectrum", NULL, "1991",
	"Scooby and Scrappy-Doo (48K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecSsdooRomInfo, SpecSsdooRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sergeant Seymour Robotcop

static struct BurnRomInfo SpecSergeantseymourrobotcopRomDesc[] = {
	{ "Sergeant Seymour Robotcop (1992)(Codemasters).tap", 35548, 0x2ecf649d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSergeantseymourrobotcop, SpecSergeantseymourrobotcop, Spec128)
STD_ROM_FN(SpecSergeantseymourrobotcop)

struct BurnDriver BurnSpecSergeantseymourrobotcop = {
	"spec_sergeantseymourrobotcop", NULL, "spec_spec128", NULL, "1992",
	"Sergeant Seymour Robotcop\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSergeantseymourrobotcopRomInfo, SpecSergeantseymourrobotcopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sky High Stuntman

static struct BurnRomInfo SpecSkyhighstuntmanRomDesc[] = {
	{ "Sky High Stuntman (1991)(Codemasters).tap", 50525, 0x45b71f16, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSkyhighstuntman, SpecSkyhighstuntman, Spec128)
STD_ROM_FN(SpecSkyhighstuntman)

struct BurnDriver BurnSpecSkyhighstuntman = {
	"spec_skyhighstuntman", NULL, "spec_spec128", NULL, "1991",
	"Sky High Stuntman\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSkyhighstuntmanRomInfo, SpecSkyhighstuntmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Soccer Pinball

static struct BurnRomInfo SpecSoccerpinballRomDesc[] = {
	{ "Soccer Pinball (1992)(Codemasters).tap", 61677, 0xc1c3e3b6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSoccerpinball, SpecSoccerpinball, Spec128)
STD_ROM_FN(SpecSoccerpinball)

struct BurnDriver BurnSpecSoccerpinball = {
	"spec_soccerpinball", NULL, "spec_spec128", NULL, "1992",
	"Soccer Pinball\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSoccerpinballRomInfo, SpecSoccerpinballRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Spike in Transylvania

static struct BurnRomInfo SpecSpikeintransylvaniaRomDesc[] = {
	{ "Spike in Transylvania (1991)(Codemasters).tap", 54232, 0x2e3cf5ca, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSpikeintransylvania, SpecSpikeintransylvania, Spec128)
STD_ROM_FN(SpecSpikeintransylvania)

struct BurnDriver BurnSpecSpikeintransylvania = {
	"spec_spikeintransylvania", NULL, "spec_spec128", NULL, "1991",
	"Spike in Transylvania\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSpikeintransylvaniaRomInfo, SpecSpikeintransylvaniaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Dragon

static struct BurnRomInfo SpecStardragonRomDesc[] = {
	{ "Star Dragon (1991)(Ultrasoft).tap", 34469, 0xf7a81a91, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStardragon, SpecStardragon, Spec128)
STD_ROM_FN(SpecStardragon)

struct BurnDriver BurnSpecStardragon = {
	"spec_stardragon", NULL, "spec_spec128", NULL, "1991",
	"Star Dragon\0", NULL, "Ultrasoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStardragonRomInfo, SpecStardragonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Paws (128K)

static struct BurnRomInfo SpecStarpawsRomDesc[] = {
	{ "Star Paws (1988)(Software Projects).z80", 50264, 0x14d2c355, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarpaws, SpecStarpaws, Spec128)
STD_ROM_FN(SpecStarpaws)

struct BurnDriver BurnSpecStarpaws = {
	"spec_starpaws", NULL, "spec_spec128", NULL, "1988",
	"Star Paws (128K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecStarpawsRomInfo, SpecStarpawsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Star Runner

static struct BurnRomInfo SpecStarrunnerRomDesc[] = {
	{ "Star Runner (1987)(Codemasters).tap", 56277, 0xfbde3432, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarrunner, SpecStarrunner, Spec128)
STD_ROM_FN(SpecStarrunner)

struct BurnDriver BurnSpecStarrunner = {
	"spec_starrunner", NULL, "spec_spec128", NULL, "1987",
	"Star Runner\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStarrunnerRomInfo, SpecStarrunnerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Street Gang Football

static struct BurnRomInfo SpecStreetgangfootballRomDesc[] = {
	{ "Street Gang Football (1989)(Codemasters).tap", 43537, 0x580c88d2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStreetgangfootball, SpecStreetgangfootball, Spec128)
STD_ROM_FN(SpecStreetgangfootball)

struct BurnDriver BurnSpecStreetgangfootball = {
	"spec_streetgangfootball", NULL, "spec_spec128", NULL, "1989",
	"Street Gang Football\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStreetgangfootballRomInfo, SpecStreetgangfootballRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stryker - In the Crypts of Trogan

static struct BurnRomInfo SpecStrykercryptsoftroganRomDesc[] = {
	{ "Stryker - In the Crypts of Trogan (1992)(Codemasters).tap", 90060, 0xd24f539d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStrykercryptsoftrogan, SpecStrykercryptsoftrogan, Spec128)
STD_ROM_FN(SpecStrykercryptsoftrogan)

struct BurnDriver BurnSpecStrykercryptsoftrogan = {
	"spec_strykercryptsoftrogan", NULL, "spec_spec128", NULL, "1992",
	"Stryker - In the Crypts of Trogan\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStrykercryptsoftroganRomInfo, SpecStrykercryptsoftroganRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stunt Man Seymour

static struct BurnRomInfo SpecStuntmanseymourRomDesc[] = {
	{ "Stunt Man Seymour (1992)(Codemasters).tap", 51574, 0x098c5ba8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStuntmanseymour, SpecStuntmanseymour, Spec128)
STD_ROM_FN(SpecStuntmanseymour)

struct BurnDriver BurnSpecStuntmanseymour = {
	"spec_stuntmanseymour", NULL, "spec_spec128", NULL, "1992",
	"Stunt Man Seymour\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecStuntmanseymourRomInfo, SpecStuntmanseymourRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Superbike Trans-Am

static struct BurnRomInfo SpecSuperbiketransamRomDesc[] = {
	{ "Superbike Trans-Am (1989)(Codemasters).z80", 40870, 0x28322d99, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperbiketransam, SpecSuperbiketransam, Spec128)
STD_ROM_FN(SpecSuperbiketransam)

struct BurnDriver BurnSpecSuperbiketransam = {
	"spec_superbiketransam", NULL, "spec_spec128", NULL, "1989",
	"Superbike Trans-Am\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperbiketransamRomInfo, SpecSuperbiketransamRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Dragon Slayer

static struct BurnRomInfo SpecSuperdragonslayerRomDesc[] = {
	{ "Super Dragon Slayer (1989)(Codemasters).tap", 42607, 0x95581414, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperdragonslayer, SpecSuperdragonslayer, Spec128)
STD_ROM_FN(SpecSuperdragonslayer)

struct BurnDriver BurnSpecSuperdragonslayer = {
	"spec_superdragonslayer", NULL, "spec_spec128", NULL, "1989",
	"Super Dragon Slayer\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperdragonslayerRomInfo, SpecSuperdragonslayerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super G-Man

static struct BurnRomInfo SpecSupergmanRomDesc[] = {
	{ "Super G-Man (1987)(Codemasters).tap", 44091, 0x4e83dd70, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSupergman, SpecSupergman, Spec128)
STD_ROM_FN(SpecSupergman)

struct BurnDriver BurnSpecSupergman = {
	"spec_supergman", NULL, "spec_spec128", NULL, "1987",
	"Super G-Man\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSupergmanRomInfo, SpecSupergmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Hero

static struct BurnRomInfo SpecSuperheroRomDesc[] = {
	{ "Super Hero (1988)(Codemasters).tap", 51573, 0x26d19439, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperhero, SpecSuperhero, Spectrum)
STD_ROM_FN(SpecSuperhero)

struct BurnDriver BurnSpecSuperhero = {
	"spec_superhero", NULL, "spec_spectrum", NULL, "1988",
	"Super Hero\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperheroRomInfo, SpecSuperheroRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Robin Hood

static struct BurnRomInfo SpecSuperrobinhoodRomDesc[] = {
	{ "Super Robin Hood (1987)(Codemasters).tap", 47900, 0x61d90998, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperrobinhood, SpecSuperrobinhood, Spectrum)
STD_ROM_FN(SpecSuperrobinhood)

struct BurnDriver BurnSpecSuperrobinhood = {
	"spec_superrobinhood", NULL, "spec_spectrum", NULL, "1987",
	"Super Robin Hood\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperrobinhoodRomInfo, SpecSuperrobinhoodRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Scramble Simulator (128K)

static struct BurnRomInfo SpecSuperscramblesimRomDesc[] = {
	{ "Super Scramble Simulator (1989)(Gremlin Graphics).tap", 56996, 0x1454d1d7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperscramblesim, SpecSuperscramblesim, Spec128)
STD_ROM_FN(SpecSuperscramblesim)

struct BurnDriver BurnSpecSuperscramblesim = {
	"spec_superscramblesim", NULL, "spec_spec128", NULL, "1989",
	"Super Scramble Simulator (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecSuperscramblesimRomInfo, SpecSuperscramblesimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Seymour Saves the Planet

static struct BurnRomInfo SpecSuperseymoursavestheplanetRomDesc[] = {
	{ "Super Seymour Saves the Planet (1991)(Codemasters).tap", 65869, 0xd422deed, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperseymoursavestheplanet, SpecSuperseymoursavestheplanet, Spec128)
STD_ROM_FN(SpecSuperseymoursavestheplanet)

struct BurnDriver BurnSpecSuperseymoursavestheplanet = {
	"spec_superseymoursavestheplanet", NULL, "spec_spec128", NULL, "1991",
	"Super Seymour Saves the Planet\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperseymoursavestheplanetRomInfo, SpecSuperseymoursavestheplanetRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Stuntman

static struct BurnRomInfo SpecSuperstuntmanRomDesc[] = {
	{ "Super Stuntman (1988)(Codemasters).tap", 43549, 0x984b4f12, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSuperstuntman, SpecSuperstuntman, Spectrum)
STD_ROM_FN(SpecSuperstuntman)

struct BurnDriver BurnSpecSuperstuntman = {
	"spec_superstuntman", NULL, "spec_spectrum", NULL, "1988",
	"Super Stuntman\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSuperstuntmanRomInfo, SpecSuperstuntmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Tank Simulator

static struct BurnRomInfo SpecSupertanksimRomDesc[] = {
	{ "Super Tank Simulator (1989)(Codemasters).tap", 44848, 0xd88ed48a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSupertanksim, SpecSupertanksim, Spec128)
STD_ROM_FN(SpecSupertanksim)

struct BurnDriver BurnSpecSupertanksim = {
	"spec_supertanksim", NULL, "spec_spec128", NULL, "1989",
	"Super Tank Simulator\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSupertanksimRomInfo, SpecSupertanksimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tarzan

static struct BurnRomInfo SpecTarzanRomDesc[] = {
	{ "Tarzan (1986)(Martech Games).tap", 56277, 0xe95f4ac1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTarzan, SpecTarzan, Spec128)
STD_ROM_FN(SpecTarzan)

struct BurnDriver BurnSpecTarzan = {
	"spec_tarzan", NULL, "spec_spec128", NULL, "1986",
	"Tarzan\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTarzanRomInfo, SpecTarzanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tarzan Goes Ape

static struct BurnRomInfo SpecTarzangoesapeRomDesc[] = {
	{ "Tarzan Goes Ape (1991)(Codemasters).tap", 47470, 0x558dadb9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTarzangoesape, SpecTarzangoesape, Spec128)
STD_ROM_FN(SpecTarzangoesape)

struct BurnDriver BurnSpecTarzangoesape = {
	"spec_tarzangoesape", NULL, "spec_spec128", NULL, "1991",
	"Tarzan Goes Ape\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTarzangoesapeRomInfo, SpecTarzangoesapeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Terra Cognita

static struct BurnRomInfo SpecTerracognitaRomDesc[] = {
	{ "Terra Cognita (1986)(Codemasters).tap", 42714, 0x2ca5d961, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTerracognita, SpecTerracognita, Spec128)
STD_ROM_FN(SpecTerracognita)

struct BurnDriver BurnSpecTerracognita = {
	"spec_terracognita", NULL, "spec_spec128", NULL, "1986",
	"Terra Cognita\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTerracognitaRomInfo, SpecTerracognitaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Living Daylights

static struct BurnRomInfo SpecThelivingdaylightsRomDesc[] = {
	{ "The Living Daylights (1987)(Domark).tap", 38808, 0x9945b8ca, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThelivingdaylights, SpecThelivingdaylights, Spec128)
STD_ROM_FN(SpecThelivingdaylights)

struct BurnDriver BurnSpecThelivingdaylights = {
	"spec_thelivingdaylights", NULL, "spec_spec128", NULL, "1987",
	"The Living Daylights\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThelivingdaylightsRomInfo, SpecThelivingdaylightsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tilt

static struct BurnRomInfo SpecTiltRomDesc[] = {
	{ "Tilt (1990)(Codemasters).tap", 39893, 0xa03922c0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTilt, SpecTilt, Spec128)
STD_ROM_FN(SpecTilt)

struct BurnDriver BurnSpecTilt = {
	"spec_tilt", NULL, "spec_spec128", NULL, "1990",
	"Tilt\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTiltRomInfo, SpecTiltRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tornado ECR

static struct BurnRomInfo SpecTornadoecrRomDesc[] = {
	{ "Tornado ECR (1991)(Codemasters).tap", 47225, 0x7fe902cb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTornadoecr, SpecTornadoecr, Spec128)
STD_ROM_FN(SpecTornadoecr)

struct BurnDriver BurnSpecTornadoecr = {
	"spec_tornadoecr", NULL, "spec_spec128", NULL, "1991",
	"Tornado ECR\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTornadoecrRomInfo, SpecTornadoecrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Transmuter

static struct BurnRomInfo SpecTransmuterRomDesc[] = {
	{ "Transmuter (1987)(Codemasters).tap", 48599, 0x626052a5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTransmuter, SpecTransmuter, Spectrum)
STD_ROM_FN(SpecTransmuter)

struct BurnDriver BurnSpecTransmuter = {
	"spec_transmuter", NULL, "spec_spectrum", NULL, "1987",
	"Transmuter\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTransmuterRomInfo, SpecTransmuterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Turbo Girl

static struct BurnRomInfo SpecTurbogirlRomDesc[] = {
	{ "Turbo Girl (1988)(Dinamic).tap", 48800, 0x358aaac4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTurbogirl, SpecTurbogirl, Spec128)
STD_ROM_FN(SpecTurbogirl)

struct BurnDriver BurnSpecTurbogirl = {
	"spec_turbogirl", NULL, "spec_spec128", NULL, "1988",
	"Turbo Girl\0", NULL, "Dinamic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTurbogirlRomInfo, SpecTurbogirlRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Turbo the Tortoise

static struct BurnRomInfo SpecTurbothetortoiseRomDesc[] = {
	{ "Turbo the Tortoise (1992)(Hi-Tec Software).tap", 74768, 0x55513db0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTurbothetortoise, SpecTurbothetortoise, Spec128)
STD_ROM_FN(SpecTurbothetortoise)

struct BurnDriver BurnSpecTurbothetortoise = {
	"spec_turbothetortoise", NULL, "spec_spec128", NULL, "1992",
	"Turbo the Tortoise\0", NULL, "Hi-Tec Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTurbothetortoiseRomInfo, SpecTurbothetortoiseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Twin Turbo V8

static struct BurnRomInfo SpecTwinturbov8RomDesc[] = {
	{ "Twin Turbo V8 (1989)(Codemasters).tap", 49133, 0x98b8d8ff, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTwinturbov8, SpecTwinturbov8, Spec128)
STD_ROM_FN(SpecTwinturbov8)

struct BurnDriver BurnSpecTwinturbov8 = {
	"spec_twinturbov8", NULL, "spec_spec128", NULL, "1989",
	"Twin Turbo V8\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTwinturbov8RomInfo, SpecTwinturbov8RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vixen - Part 1

static struct BurnRomInfo SpecVixen1RomDesc[] = {
	{ "Vixen - Part 1 (1988)(Martech Games).tap", 49543, 0xe5d1c4a9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecVixen1, SpecVixen1, Spec128)
STD_ROM_FN(SpecVixen1)

struct BurnDriver BurnSpecVixen1 = {
	"spec_vixen1", NULL, "spec_spec128", NULL, "1988",
	"Vixen - Part 1\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecVixen1RomInfo, SpecVixen1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vixen - Part 2

static struct BurnRomInfo SpecVixen2RomDesc[] = {
	{ "Vixen - Part 2 (1988)(Martech Games).tap", 49547, 0xba54ec0b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecVixen2, SpecVixen2, Spec128)
STD_ROM_FN(SpecVixen2)

struct BurnDriver BurnSpecVixen2 = {
	"spec_vixen2", "spec_vixen1", "spec_spec128", NULL, "1988",
	"Vixen - Part 2\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecVixen2RomInfo, SpecVixen2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vixen - Part 3

static struct BurnRomInfo SpecVixen3RomDesc[] = {
	{ "Vixen - Part 3 (1988)(Martech Games).tap", 49547, 0x1d0845d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecVixen3, SpecVixen3, Spec128)
STD_ROM_FN(SpecVixen3)

struct BurnDriver BurnSpecVixen3 = {
	"spec_vixen3", "spec_vixen1", "spec_spec128", NULL, "1988",
	"Vixen - Part 3\0", NULL, "Martech Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecVixen3RomInfo, SpecVixen3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wacky Darts (128K)

static struct BurnRomInfo SpecWackydartsRomDesc[] = {
	{ "Wacky Darts (1991)(Codemasters)(128K).z80", 58652, 0x6790db75, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWackydarts, SpecWackydarts, Spec128)
STD_ROM_FN(SpecWackydarts)

struct BurnDriver BurnSpecWackydarts = {
	"spec_wackydarts", NULL, "spec_spec128", NULL, "1991",
	"Wacky Darts (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWackydartsRomInfo, SpecWackydartsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// White Heat

static struct BurnRomInfo SpecWhiteheatRomDesc[] = {
	{ "White Heat (1987)(Codemasters).tap", 35611, 0x37f5048a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWhiteheat, SpecWhiteheat, Spectrum)
STD_ROM_FN(SpecWhiteheat)

struct BurnDriver BurnSpecWhiteheat = {
	"spec_whiteheat", NULL, "spec_spectrum", NULL, "1987",
	"White Heat\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecWhiteheatRomInfo, SpecWhiteheatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wizard Willy (128K)

static struct BurnRomInfo SpecWizardwillyRomDesc[] = {
	{ "Wizard Willy (1990)(Codemasters).tap", 45968, 0x42cd6c9d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWizardwilly, SpecWizardwilly, Spec128)
STD_ROM_FN(SpecWizardwilly)

struct BurnDriver BurnSpecWizardwilly = {
	"spec_wizardwilly", NULL, "spec_spec128", NULL, "1990",
	"Wizard Willy (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecWizardwillyRomInfo, SpecWizardwillyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wrestling Superstars (128K)

static struct BurnRomInfo SpecWrestlingsuperstarsRomDesc[] = {
	{ "Wrestling Superstars (1993)(Codemasters).z80", 41173, 0x0b21ab8e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWrestlingsuperstars, SpecWrestlingsuperstars, Spec128)
STD_ROM_FN(SpecWrestlingsuperstars)

struct BurnDriver BurnSpecWrestlingsuperstars = {
	"spec_wrestlingsuperstars", NULL, "spec_spec128", NULL, "1993",
	"Wrestling Superstars (128K)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecWrestlingsuperstarsRomInfo, SpecWrestlingsuperstarsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hero Quest (128K)

static struct BurnRomInfo SpecHeroquestRomDesc[] = {
	{ "Hero Quest (1991)(Gremlin Graphics).tap", 111870, 0x76faec29, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHeroquest, SpecHeroquest, Spec128)
STD_ROM_FN(SpecHeroquest)

struct BurnDriver BurnSpecHeroquest = {
	"spec_heroquest", NULL, "spec_spec128", NULL, "1991",
	"Hero Quest (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RPG, 0,
	SpectrumGetZipName, SpecHeroquestRomInfo, SpecHeroquestRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hero Quest - Return of the Witch Lord (128K)

static struct BurnRomInfo SpecHeroquestwitchlordRomDesc[] = {
	{ "Hero Quest - Return of the Witch Lord (1991)(Gremlin Graphics).tap", 131173, 0x65141180, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHeroquestwitchlord, SpecHeroquestwitchlord, Spec128)
STD_ROM_FN(SpecHeroquestwitchlord)

struct BurnDriver BurnSpecHeroquestwitchlord = {
	"spec_heroquestwitchlord", NULL, "spec_spec128", NULL, "1991",
	"Hero Quest - Return of the Witch Lord (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RPG, 0,
	SpectrumGetZipName, SpecHeroquestwitchlordRomInfo, SpecHeroquestwitchlordRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// ATF - Advanced Tactical Fighter (128K)

static struct BurnRomInfo SpecAtf128kRomDesc[] = {
	{ "ATF 128K (1988)(Digital Integration).z80", 43769, 0x23f0731e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAtf128k, SpecAtf128k, Spec128)
STD_ROM_FN(SpecAtf128k)

struct BurnDriver BurnSpecAtf128k = {
	"spec_atf128k", NULL, "spec_spec128", NULL, "1988",
	"ATF - Advanced Tactical Fighter (128K)\0", NULL, "Digital Integration", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SIM | GBF_SHOOT, 0,
	SpectrumGetZipName, SpecAtf128kRomInfo, SpecAtf128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hercules - Slayer of the Damned (128K)

static struct BurnRomInfo SpecHercules128kRomDesc[] = {
	{ "Hercules - Slayer of the Damned 128K (1988)(Gremlin Graphics).z80", 97391, 0x58b28530, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHercules128k, SpecHercules128k, Spec128)
STD_ROM_FN(SpecHercules128k)

struct BurnDriver BurnSpecHercules128k = {
	"spec_hercules128k", NULL, "spec_spec128", NULL, "1988",
	"Hercules - Slayer of the Damned (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecHercules128kRomInfo, SpecHercules128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hercules - Slayer of the Damned (48K)

static struct BurnRomInfo SpecHercules48kRomDesc[] = {
	{ "Hercules - Slayer of the Damned 48K (1988)(Gremlin Graphics).z80", 42577, 0x234e953e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHercules48k, SpecHercules48k, Spectrum)
STD_ROM_FN(SpecHercules48k)

struct BurnDriver BurnSpecHercules48k = {
	"spec_hercules48k", "spec_hercules128k", "spec_spectrum", NULL, "1988",
	"Hercules - Slayer of the Damned (48K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecHercules48kRomInfo, SpecHercules48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Klax (128K)

static struct BurnRomInfo SpecKlaxRomDesc[] = {
	{ "Klax 128K (1990)(Domark).z80", 89781, 0xd1f93544, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKlax, SpecKlax, Spec128)
STD_ROM_FN(SpecKlax)

struct BurnDriver BurnSpecKlax = {
	"spec_klax", NULL, "spec_spec128", NULL, "1990",
	"Klax (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecKlaxRomInfo, SpecKlaxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mickey Mouse - The Computer Game (128K)

static struct BurnRomInfo SpecMickeymouseRomDesc[] = {
	{ "Mickey Mouse - The Computer Game 128K (1988)(Gremlin Graphics).z80", 44794, 0x2dce1ffe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMickeymouse, SpecMickeymouse, Spec128)
STD_ROM_FN(SpecMickeymouse)

struct BurnDriver BurnSpecMickeymouse = {
	"spec_mickeymouse", NULL, "spec_spec128", NULL, "1988",
	"Mickey Mouse - The Computer Game (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMickeymouseRomInfo, SpecMickeymouseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Road Blasters (128K)

static struct BurnRomInfo SpecRblasterRomDesc[] = {
	{ "Road Blasters 128K (1988)(U.S. Gold).z80", 99688, 0x64455e8a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRblaster, SpecRblaster, Spec128)
STD_ROM_FN(SpecRblaster)

struct BurnDriver BurnSpecRblaster = {
	"spec_rblaster", NULL, "spec_spec128", NULL, "1988",
	"Road Blasters (128K)\0", NULL, "U.S. Gold", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING | GBF_SHOOT, 0,
	SpectrumGetZipName, SpecRblasterRomInfo, SpecRblasterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Road Runner and Wile E. Coyote (48K)

static struct BurnRomInfo SpecRoadwileRomDesc[] = {
	{ "Road Runner and Wile E. Coyote 48K (1991)(Hi-Tec).z80", 32844, 0x68c9c307, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRoadwile, SpecRoadwile, Spectrum)
STD_ROM_FN(SpecRoadwile)

struct BurnDriver BurnSpecRoadwile = {
	"spec_roadwile", "spec_roadwile128k", "spec_spectrum", NULL, "1991",
	"Road Runner and Wile E. Coyote (48K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecRoadwileRomInfo, SpecRoadwileRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Road Runner and Wile E. Coyote (128K)

static struct BurnRomInfo SpecRoadwile128kRomDesc[] = {
	{ "Road Runner and Wile E. Coyote 128K (1991)(Hi-Tec).tap", 50312, 0x766bd6a4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRoadwile128k, SpecRoadwile128k, Spec128)
STD_ROM_FN(SpecRoadwile128k)

struct BurnDriver BurnSpecRoadwile128k = {
	"spec_roadwile128k", NULL, "spec_spec128", NULL, "1991",
	"Road Runner and Wile E. Coyote (128K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecRoadwile128kRomInfo, SpecRoadwile128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Samurai Warrior - Usagi Yojimbo (48K)

static struct BurnRomInfo SpecSamwarriorRomDesc[] = {
	{ "Samurai Warrior - Usagi Yojimbo (1988)(Firebird).z80", 44210, 0x0b36f4a1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSamwarrior, SpecSamwarrior, Spectrum)
STD_ROM_FN(SpecSamwarrior)

struct BurnDriver BurnSpecSamwarrior = {
	"spec_samwarrior", "spec_samwarrior128k", "spec_spectrum", NULL, "1988",
	"Samurai Warrior - Usagi Yojimbo (48K)\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecSamwarriorRomInfo, SpecSamwarriorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Samurai Warrior - Usagi Yojimbo (128K)

static struct BurnRomInfo SpecSamwarrior128kRomDesc[] = {
	{ "Samurai Warrior - Usagi Yojimbo 128k (1988)(Firebird).tap", 53473, 0x989e9ffd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSamwarrior128k, SpecSamwarrior128k, Spec128)
STD_ROM_FN(SpecSamwarrior128k)

struct BurnDriver BurnSpecSamwarrior128k = {
	"spec_samwarrior128k", NULL, "spec_spec128", NULL, "1988",
	"Samurai Warrior - Usagi Yojimbo (128K)\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecSamwarrior128kRomInfo, SpecSamwarrior128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thundercats (128K)

static struct BurnRomInfo SpecTcats128kRomDesc[] = {
	{ "Thundercats 128K (1987)(Elite Systems).z80", 81713, 0xe6b612c8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTcats128k, SpecTcats128k, Spec128)
STD_ROM_FN(SpecTcats128k)

struct BurnDriver BurnSpecTcats128k = {
	"spec_tcats128k", NULL, "spec_spec128", NULL, "1987",
	"Thundercats (128K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecTcats128kRomInfo, SpecTcats128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thundercats (48K)

static struct BurnRomInfo SpecTcats48kRomDesc[] = {
	{ "Thundercats 48K (1987)(Elite Systems).z80", 39679, 0x2812ba20, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTcats48k, SpecTcats48k, Spectrum)
STD_ROM_FN(SpecTcats48k)

struct BurnDriver BurnSpecTcats48k = {
	"spec_tcats48k", "spec_tcats128k", "spec_spectrum", NULL, "1987",
	"Thundercats (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecTcats48kRomInfo, SpecTcats48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hammerfist (48K)

static struct BurnRomInfo SpecHammerfistRomDesc[] = {
	{ "Hammerfist (1990)(Activision).tap", 188831, 0xc6ad835b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHammerfist, SpecHammerfist, Spectrum)
STD_ROM_FN(SpecHammerfist)

struct BurnDriver BurnSpecHammerfist = {
	"spec_hammerfist", NULL, "spec_spectrum", NULL, "1990",
	"Hammerfist (48K)\0", NULL, "Activision", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecHammerfistRomInfo, SpecHammerfistRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hong Kong Phooey (48K)

static struct BurnRomInfo SpecHkphooeyRomDesc[] = {
	{ "Hong Kong Phooey 48K (1990)(Hi-Tec).z80", 37410, 0x444a5064, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHkphooey, SpecHkphooey, Spectrum)
STD_ROM_FN(SpecHkphooey)

struct BurnDriver BurnSpecHkphooey = {
	"spec_hkphooey", NULL, "spec_spectrum", NULL, "1990",
	"Hong Kong Phooey (48K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecHkphooeyRomInfo, SpecHkphooeyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hysteria (48K)

static struct BurnRomInfo SpecHysteriaRomDesc[] = {
	{ "Hysteria 48K (1987)(Software Projects).tap", 47426, 0x70fcbb59, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHysteria, SpecHysteria, Spectrum)
STD_ROM_FN(SpecHysteria)

struct BurnDriver BurnSpecHysteria = {
	"spec_hysteria", NULL, "spec_spectrum", NULL, "1987",
	"Hysteria (48K)\0", NULL, "Software Projects", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecHysteriaRomInfo, SpecHysteriaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// International Karate (48K) - Side 1

static struct BurnRomInfo SpecIntkarates1RomDesc[] = {
	{ "International Karate 48K - Side 1 (1985)(System 3).z80", 34724, 0x344134a1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecIntkarates1, SpecIntkarates1, Spectrum)
STD_ROM_FN(SpecIntkarates1)

struct BurnDriver BurnSpecIntkarates1 = {
	"spec_intkarates1", NULL, "spec_spectrum", NULL, "1985",
	"International Karate (48K) - Side 1\0", NULL, "System 3", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 2, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecIntkarates1RomInfo, SpecIntkarates1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// International Karate (48K) - Side 2

static struct BurnRomInfo SpecIntkarates2RomDesc[] = {
	{ "International Karate 48K - Side 2 (1985)(System 3).z80", 34746, 0x28eac074, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecIntkarates2, SpecIntkarates2, Spectrum)
STD_ROM_FN(SpecIntkarates2)

struct BurnDriver BurnSpecIntkarates2 = {
	"spec_intkarates2", "spec_intkarates1", "spec_spectrum", NULL, "1985",
	"International Karate (48K) - Side 2\0", NULL, "System 3", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 2, HARDWARE_SPECTRUM, GBF_VSFIGHT, 0,
	SpectrumGetZipName, SpecIntkarates2RomInfo, SpecIntkarates2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pentagram (48K)

static struct BurnRomInfo SpecPentagramRomDesc[] = {
	{ "Pentagram 48K (1986)(Ultimate).tap", 38524, 0x4d43b15a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPentagram, SpecPentagram, Spectrum)
STD_ROM_FN(SpecPentagram)

struct BurnDriver BurnSpecPentagram = {
	"spec_pentagram", NULL, "spec_spectrum", NULL, "1986",
	"Pentagram (48K)\0", NULL, "Ultimate", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, SpecPentagramRomInfo, SpecPentagramRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pyjamarama (48K)

static struct BurnRomInfo SpecPyjamaramaRomDesc[] = {
	{ "Pyjamarama 48K (1984)(Mikro-Gen).z80", 42069, 0x6801d962, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPyjamarama, SpecPyjamarama, Spectrum)
STD_ROM_FN(SpecPyjamarama)

struct BurnDriver BurnSpecPyjamarama = {
	"spec_pyjamarama", NULL, "spec_spectrum", NULL, "1984",
	"Pyjamarama (48K)\0", NULL, "Mikro-Gen", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecPyjamaramaRomInfo, SpecPyjamaramaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Scooby Doo in the Castle Mystery (48K)

static struct BurnRomInfo SpecScoobydooRomDesc[] = {
	{ "Scooby Doo in the Castle Mystery 48K (1986)(Elite Systems).z80", 32957, 0x031b9d4c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecScoobydoo, SpecScoobydoo, Spectrum)
STD_ROM_FN(SpecScoobydoo)

struct BurnDriver BurnSpecScoobydoo = {
	"spec_scoobydoo", NULL, "spec_spectrum", NULL, "1986",
	"Scooby Doo in the Castle Mystery (48K)\0", NULL, "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecScoobydooRomInfo, SpecScoobydooRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Super Cars (128K)

static struct BurnRomInfo SpecSupercarsRomDesc[] = {
	{ "Super Cars 128K (1990)(Gremlin Graphics).z80", 88269, 0x5a2d86ca, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSupercars, SpecSupercars, Spec128)
STD_ROM_FN(SpecSupercars)

struct BurnDriver BurnSpecSupercars = {
	"spec_supercars", NULL, "spec_spec128", NULL, "1990",
	"Super Cars (128K)\0", NULL, "Gremlin Graphics", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecSupercarsRomInfo, SpecSupercarsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Total Eclipse (48K)

static struct BurnRomInfo SpecTeclipseRomDesc[] = {
	{ "Total Eclipse (1988)(Incentive).z80", 40490, 0xd863549f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTeclipse, SpecTeclipse, Spectrum)
STD_ROM_FN(SpecTeclipse)

struct BurnDriver BurnSpecTeclipse = {
	"spec_teclipse", NULL, "spec_spectrum", NULL, "1988",
	"Total Eclipse (48K)\0", NULL, "Incentive", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT | GBF_ADV, 0,
	SpectrumGetZipName, SpecTeclipseRomInfo, SpecTeclipseRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thanatos (128K)

static struct BurnRomInfo SpecThanatos128KRomDesc[] = {
	{ "Thanatos 128K (1986)(Durell).z80", 43983, 0xec881ae7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThanatos128K, SpecThanatos128K, Spec128)
STD_ROM_FN(SpecThanatos128K)

struct BurnDriver BurnSpecThanatos128K = {
	"spec_thanatos128K", NULL, "spec_spec128", NULL, "1986",
	"Thanatos (128K)\0", NULL, "Durell", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecThanatos128KRomInfo, SpecThanatos128KRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Thanatos (48K)

static struct BurnRomInfo SpecThanatos48KRomDesc[] = {
	{ "Thanatos 48K (1986)(Durell).z80", 44946, 0x02c57557, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThanatos48K, SpecThanatos48K, Spectrum)
STD_ROM_FN(SpecThanatos48K)

struct BurnDriver BurnSpecThanatos48K = {
	"spec_thanatos48K", "spec_thanatos128K", "spec_spectrum", NULL, "1986",
	"Thanatos (48K)\0", NULL, "Durell", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecThanatos48KRomInfo, SpecThanatos48KRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alchemist (48K)

static struct BurnRomInfo SpecAlchemistRomDesc[] = {
	{ "Alchemist (1983)(Imagine).z80", 37807, 0xe6f0df00, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAlchemist, SpecAlchemist, Spectrum)
STD_ROM_FN(SpecAlchemist)

struct BurnDriver BurnSpecAlchemist = {
	"spec_alchemist", NULL, "spec_spectrum", NULL, "1983",
	"Alchemist (48K)\0", NULL, "Imagine", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAlchemistRomInfo, SpecAlchemistRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Buggy Boy (128K)

static struct BurnRomInfo SpecBuggyboyRomDesc[] = {
	{ "Buggy Boy 128K (1988)(Elite Systems).z80", 59830, 0x0497d4f4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBuggyboy, SpecBuggyboy, Spec128)
STD_ROM_FN(SpecBuggyboy)

struct BurnDriver BurnSpecBuggyboy = {
	"spec_buggyboy", NULL, "spec_spec128", NULL, "1988",
	"Buggy Boy (128K)\0", "P, L and Space to select options", "Elite Systems", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecBuggyboyRomInfo, SpecBuggyboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Flash Gordon (128K)

static struct BurnRomInfo SpecFlashgordonRomDesc[] = {
	{ "Flash Gordon 128K (Mastertronic).z80", 74258, 0x07065677, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFlashgordon, SpecFlashgordon, Spec128)
STD_ROM_FN(SpecFlashgordon)

struct BurnDriver BurnSpecFlashgordon = {
	"spec_flashgordon", NULL, "spec_spec128", NULL, "1987",
	"Flash Gordon (128K)\0", NULL, "Mastertronic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecFlashgordonRomInfo, SpecFlashgordonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Garfield - Winter's Tail (48K)

static struct BurnRomInfo SpecGarfield2RomDesc[] = {
	{ "Garfield - Winter's Tail 48K (1990)(The Edge).tap", 58470, 0x74f9f23e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGarfield2, SpecGarfield2, Spectrum)
STD_ROM_FN(SpecGarfield2)

struct BurnDriver BurnSpecGarfield2 = {
	"spec_garfield2", NULL, "spec_spectrum", NULL, "1990",
	"Garfield - Winter's Tail (48K)\0", NULL, "The Edge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecGarfield2RomInfo, SpecGarfield2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Kinetik (48K)

static struct BurnRomInfo SpecKinetikRomDesc[] = {
	{ "Kinetik 48K (1987)(Firebird).z80", 43221, 0x39a23892, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKinetik, SpecKinetik, Spectrum)
STD_ROM_FN(SpecKinetik)

struct BurnDriver BurnSpecKinetik = {
	"spec_kinetik", NULL, "spec_spectrum", NULL, "1987",
	"Kinetik (48K)\0", NULL, "Firebird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecKinetikRomInfo, SpecKinetikRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// LED Storm - Rally 2011 (128K)

static struct BurnRomInfo SpecLedstormRomDesc[] = {
	{ "LED Storm 128K (1988)(Go!).tap", 86247, 0xd20b9e67, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLedstorm, SpecLedstorm, Spec128)
STD_ROM_FN(SpecLedstorm)

struct BurnDriver BurnSpecLedstorm = {
	"spec_ledstorm", NULL, "spec_spec128", NULL, "1988",
	"LED Storm - Rally 2011 (128K)\0", NULL, "Go!", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_RACING, 0,
	SpectrumGetZipName, SpecLedstormRomInfo, SpecLedstormRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.T.U.N. Runner (128K)

static struct BurnRomInfo SpecStunrun128kRomDesc[] = {
	{ "S.T.U.N. Runner 128K (1990)(Domark).z80", 54349, 0xe6abdc96, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStunrun128k, SpecStunrun128k, Spec128)
STD_ROM_FN(SpecStunrun128k)

struct BurnDriver BurnSpecStunrun128k = {
	"spec_stunrun128k", NULL, "spec_spec128", NULL, "1990",
	"S.T.U.N. Runner (128K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_RACING | GBF_SHOOT, 0,
	SpectrumGetZipName, SpecStunrun128kRomInfo, SpecStunrun128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.T.U.N. Runner (48K)

static struct BurnRomInfo SpecStunrun48kRomDesc[] = {
	{ "S.T.U.N. Runner 48K (1990)(Domark).z80", 35693, 0x96a08e01, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStunrun48k, SpecStunrun48k, Spectrum)
STD_ROM_FN(SpecStunrun48k)

struct BurnDriver BurnSpecStunrun48k = {
	"spec_stunrun48k", "spec_stunrun128k", "spec_spectrum", NULL, "1990",
	"S.T.U.N. Runner (48K)\0", NULL, "Domark", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_RACING | GBF_SHOOT, 0,
	SpectrumGetZipName, SpecStunrun48kRomInfo, SpecStunrun48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Starglider (128K)

static struct BurnRomInfo SpecStarglider128kRomDesc[] = {
	{ "Starglider 128K (1986)(Rainbird).z80", 84587, 0xb1d2df07, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarglider128k, SpecStarglider128k, Spec128)
STD_ROM_FN(SpecStarglider128k)

struct BurnDriver BurnSpecStarglider128k = {
	"spec_starglider128k", NULL, "spec_spec128", NULL, "1986",
	"Starglider (128K)\0", NULL, "Rainbird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_SHOOT | GBF_SIM, 0,
	SpectrumGetZipName, SpecStarglider128kRomInfo, SpecStarglider128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Starglider (48K)

static struct BurnRomInfo SpecStarglider48kRomDesc[] = {
	{ "Starglider 48K (1986)(Rainbird).z80", 40976, 0x99cbfc54, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStarglider48k, SpecStarglider48k, Spectrum)
STD_ROM_FN(SpecStarglider48k)

struct BurnDriver BurnSpecStarglider48k = {
	"spec_starglider48k", "spec_starglider128k", "spec_spectrum", NULL, "1986",
	"Starglider (48K)\0", NULL, "Rainbird", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE, 1, HARDWARE_SPECTRUM, GBF_SHOOT | GBF_SIM, 0,
	SpectrumGetZipName, SpecStarglider48kRomInfo, SpecStarglider48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Viz - The Computer Game (128K)

static struct BurnRomInfo SpecVizRomDesc[] = {
	{ "Viz - The Computer Game 128K (1991)(Virgin Games).tap", 167642, 0x005482f2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecViz, SpecViz, Spec128)
STD_ROM_FN(SpecViz)

struct BurnDriver BurnSpecViz = {
	"spec_viz", NULL, "spec_spec128", NULL, "1991",
	"Viz - The Computer Game (128K)\0", NULL, "Virgin Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecVizRomInfo, SpecVizRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yogi Bear (48K)

static struct BurnRomInfo SpecYogibearRomDesc[] = {
	{ "Yogi Bear 48K (1987)(Piranha).z80", 43670, 0x437fa194, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYogibear, SpecYogibear, Spectrum)
STD_ROM_FN(SpecYogibear)

struct BurnDriver BurnSpecYogibear = {
	"spec_yogibear", NULL, "spec_spectrum", NULL, "1987",
	"Yogi Bear (48K)\0", NULL, "Piranha", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecYogibearRomInfo, SpecYogibearRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yogi's Great Escape (48K)

static struct BurnRomInfo SpecYogigreatescapeRomDesc[] = {
	{ "Yogi's Great Escape (1990)(Hi-Tec).tap", 54731, 0xd6afad15, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYogigreatescape, SpecYogigreatescape, Spectrum)
STD_ROM_FN(SpecYogigreatescape)

struct BurnDriver BurnSpecYogigreatescape = {
	"spec_yogigreatescape", NULL, "spec_spectrum", NULL, "1990",
	"Yogi's Great Escape (48K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecYogigreatescapeRomInfo, SpecYogigreatescapeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yogi Bear and Friends - The Greed Monster (128K)

static struct BurnRomInfo SpecYogigreedmonsterRomDesc[] = {
	{ "Yogi Bear and Friends - The Greed Monster (1990)(Hi-Tec).tap", 56556, 0x601b7f1f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYogigreedmonster, SpecYogigreedmonster, Spec128)
STD_ROM_FN(SpecYogigreedmonster)

struct BurnDriver BurnSpecYogigreedmonster = {
	"spec_yogigreedmonster", NULL, "spec_spec128", NULL, "1990",
	"Yogi Bear and Friends - The Greed Monster (128K)\0", NULL, "Hi-Tec", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, SpecYogigreedmonsterRomInfo, SpecYogigreedmonsterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Homebrew Games (Post-2000)
// Hereunder put only the HB ZX Spectrum games

// Black Star (HB)

static struct BurnRomInfo SpecblackstarRomDesc[] = {
	{ "Black Star (2015)(usebox.net).tap", 26349, 0xfd6cd75d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specblackstar, Specblackstar, Spec128)
STD_ROM_FN(Specblackstar)

struct BurnDriver BurnSpecblackstar = {
	"spec_blackstar", NULL, "spec_spec128", NULL, "2015",
	"Black Star (HB)\0", NULL, "usebox.net", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecblackstarRomInfo, SpecblackstarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// BlockZ (HB)

static struct BurnRomInfo SpecblockzRomDesc[] = {
	{ "BlockZ (2021)(Raymond Russell).tap", 24913, 0x44b94d5e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specblockz, Specblockz, Spec128)
STD_ROM_FN(Specblockz)

struct BurnDriver BurnSpecblockz = {
	"spec_blockz", NULL, "spec_spec128", NULL, "2020",
	"BlockZ (HB)\0", NULL, "Raymond Russell", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecblockzRomInfo, SpecblockzRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Brick Rick Graveyard Shift v1.03 (HB)

static struct BurnRomInfo SpecbrickrickRomDesc[] = {
	{ "Brick Rick Graveyard Shift v1.03 (2021)(usebox.net).tap", 31751, 0x5cea272c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbrickrick, Specbrickrick, Spec128)
STD_ROM_FN(Specbrickrick)

struct BurnDriver BurnSpecbrickrick = {
	"spec_brickrick", NULL, "spec_spec128", NULL, "2021",
	"Brick Rick Graveyard Shift (HB, v1.03)\0", NULL, "usebox.net", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecbrickrickRomInfo, SpecbrickrickRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Crazy Blaster (HB)

static struct BurnRomInfo SpeccblasterRomDesc[] = {
	{ "Crazy Blaster (2019)(TFW8b).tap", 32951, 0x6797d22c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccblaster, Speccblaster, Spec128)
STD_ROM_FN(Speccblaster)

struct BurnDriver BurnSpeccblaster = {
	"spec_cblaster", NULL, "spec_spec128", NULL, "2019",
	"Crazy Blaster (HB)\0", NULL, "The Future Was 8bit", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpeccblasterRomInfo, SpeccblasterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Danterrifik III (HB)

static struct BurnRomInfo Specdanterrifik3RomDesc[] = {
	{ "Danterrifik 3 (2021)(TokuSoft).tap", 46898, 0x9041c4da, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdanterrifik3, Specdanterrifik3, Spec128)
STD_ROM_FN(Specdanterrifik3)

struct BurnDriver BurnSpecdanterrifik3 = {
	"spec_danterrifik3", NULL, "spec_spec128", NULL, "2021",
	"Danterrifik III (HB)\0", NULL, "Toku Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specdanterrifik3RomInfo, Specdanterrifik3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Down the pipe (HB)

static struct BurnRomInfo SpecdownpipeRomDesc[] = {
	{ "Down the pipe (2021)(DF Design).tap", 46991, 0x71ce5665, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdownpipe, Specdownpipe, Spec128)
STD_ROM_FN(Specdownpipe)

struct BurnDriver BurnSpecdownpipe = {
	"spec_downpipe", NULL, "spec_spec128", NULL, "2021",
	"Down the pipe (HB)\0", NULL, "DF Design", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecdownpipeRomInfo, SpecdownpipeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// L'Abbaye des Morts (HB)

static struct BurnRomInfo SpeclabbayeRomDesc[] = {
	{ "L'Abbaye des Morts (2014)(Darkhorace).z80", 58110, 0xdec96ae7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclabbaye, Speclabbaye, Spec128)
STD_ROM_FN(Speclabbaye)

struct BurnDriver BurnSpeclabbaye = {
	"spec_labbaye", NULL, "spec_spec128", NULL, "2014",
	"L'Abbaye des Morts (HB)\0", "Port of original game by Locomalito and Gryzor87", "Darkhorace", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeclabbayeRomInfo, SpeclabbayeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Legend of Traxtor (HB)

static struct BurnRomInfo SpectraxtorRomDesc[] = {
	{ "Legend of Traxtor (2014)(usebox.net).tap", 23986, 0xef7dc08c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectraxtor, Spectraxtor, Spec128)
STD_ROM_FN(Spectraxtor)

struct BurnDriver BurnSpectraxtor = {
	"spec_traxtor", NULL, "spec_spec128", NULL, "2014",
	"Legend of Traxtor (HB)\0", NULL, "usebox.net", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpectraxtorRomInfo, SpectraxtorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pilot Attack v1.1 (HB)

static struct BurnRomInfo SpecpilotatkRomDesc[] = {
	{ "Pilot Attack v1.1 (2017)(TFW8b).tap", 30930, 0x8e0f7278, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specpilotatk, Specpilotatk, Spec128)
STD_ROM_FN(Specpilotatk)

struct BurnDriver BurnSpecpilotatk = {
	"spec_pilotatk", NULL, "spec_spec128", NULL, "2017",
	"Pilot Attack (HB, v1.1)\0", NULL, "The Future Was 8bit", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecpilotatkRomInfo, SpecpilotatkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Return of Traxtor (HB)

static struct BurnRomInfo Spectraxtor2RomDesc[] = {
	{ "Return of Traxtor (2015)(usebox.net).tap", 27292, 0x6fac2cd6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectraxtor2, Spectraxtor2, Spec128)
STD_ROM_FN(Spectraxtor2)

struct BurnDriver BurnSpectraxtor2 = {
	"spec_traxtor2", NULL, "spec_spec128", NULL, "2015",
	"Return of Traxtor (HB)\0", NULL, "usebox.net", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, Spectraxtor2RomInfo, Spectraxtor2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alter Ego (HB)

static struct BurnRomInfo SpecalteregoRomDesc[] = {
	{ "Alter Ego (2011)(RetroSouls).tap", 38926, 0x855c13dc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specalterego, Specalterego, Spec128)
STD_ROM_FN(Specalterego)

struct BurnDriver BurnSpecalterego = {
	"spec_alterego", NULL, "spec_spec128", NULL, "2011",
	"Alter Ego (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecalteregoRomInfo, SpecalteregoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cheril the Goddess (HB, v2)

static struct BurnRomInfo SpeccherilgodRomDesc[] = {
	{ "Cheril The Goddess V2 (2011)(Mojon Twins).tap", 43545, 0x94cc3db8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccherilgod, Speccherilgod, Spec128)
STD_ROM_FN(Speccherilgod)

struct BurnDriver BurnSpeccherilgod = {
	"spec_cherilgod", NULL, "spec_spec128", NULL, "2011",
	"Cheril the Goddess (HB, v2)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeccherilgodRomInfo, SpeccherilgodRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Danterrifik (HB)

static struct BurnRomInfo SpecdanterrifikRomDesc[] = {
	{ "Danterrifik v1.1 (2020)(Toku Soft).tap", 49067, 0x6a088600, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdanterrifik, Specdanterrifik, Spec128)
STD_ROM_FN(Specdanterrifik)

struct BurnDriver BurnSpecdanterrifik = {
	"spec_danterrifik", NULL, "spec_spec128", NULL, "2020",
	"Danterrifik (HB, v1.1)\0", NULL, "Toku Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecdanterrifikRomInfo, SpecdanterrifikRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Danterrifik 2 (HB)

static struct BurnRomInfo Specdanterrifik2RomDesc[] = {
	{ "Danterrifik 2 (2020)(Toku Soft).tap", 49982, 0x478c7adf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdanterrifik2, Specdanterrifik2, Spec128)
STD_ROM_FN(Specdanterrifik2)

struct BurnDriver BurnSpecdanterrifik2 = {
	"spec_danterrifik2", NULL, "spec_spec128", NULL, "2020",
	"Danterrifik 2 (HB)\0", NULL, "Toku Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specdanterrifik2RomInfo, Specdanterrifik2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dreamwalker - Alter Ego 2 (HB)

static struct BurnRomInfo SpecdrmwalkerRomDesc[] = {
	{ "Dreamwalker128k v1.1 (2014)(RetroSouls).tap", 92060, 0x89db756e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specdrmwalker, Specdrmwalker, Spec128)
STD_ROM_FN(Specdrmwalker)

struct BurnDriver BurnSpecdrmwalker = {
	"spec_drmwalker", NULL, "spec_spec128", NULL, "2014",
	"Dreamwalker - Alter Ego 2 (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecdrmwalkerRomInfo, SpecdrmwalkerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Endless Forms Most Beautiful (HB)

static struct BurnRomInfo SpecefmbRomDesc[] = {
	{ "Endless Forms Most Beautiful (2012)(Stonechat Productions).tap", 31843, 0x4d866f79, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specefmb, Specefmb, Spec128)
STD_ROM_FN(Specefmb)

struct BurnDriver BurnSpecefmb = {
	"spec_efmb", NULL, "spec_spec128", NULL, "2012",
	"Endless Forms Most Beautiful (HB)\0", NULL, "Stonechat Productions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 2, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecefmbRomInfo, SpecefmbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Frantic Pengo 128K (HB)

static struct BurnRomInfo Specfp128kRomDesc[] = {
	{ "frantic pengo 128k (2020)(gabam).tap", 45927, 0x66c3790a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfp128k, Specfp128k, Spec128)
STD_ROM_FN(Specfp128k)

struct BurnDriver BurnSpecfp128k = {
	"spec_fp128k", NULL, "spec_spec128", NULL, "2020",
	"Frantic Pengo 128K (HB)\0", NULL, "Gabriele Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, Specfp128kRomInfo, Specfp128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gluf (RetroSouls)

static struct BurnRomInfo SpecglufRomDesc[] = {
	{ "Gluf (2019)(RetroSouls).tap",  34669, 0x52be5093, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgluf, Specgluf, Spec128)
STD_ROM_FN(Specgluf)

struct BurnDriver BurnSpecgluf = {
	"spec_gluf", NULL, "spec_spec128", NULL, "2019",
	"GLUF (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecglufRomInfo, SpecglufRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// GraviBots (RetroSouls)

static struct BurnRomInfo SpecgravibotsRomDesc[] = {
	{ "GraviBots (2014)(RetroSouls).z80",  29436, 0x375bfa1c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specgravibots, Specgravibots, Spec128)
STD_ROM_FN(Specgravibots)

struct BurnDriver BurnSpecgravibots = {
	"spec_gravibots", NULL, "spec_spec128", NULL, "2014",
	"GraviBots (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecgravibotsRomInfo, SpecgravibotsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jumpin' Jupiter (HB)

static struct BurnRomInfo SpecjumpjupiterRomDesc[] = {
	{ "Jumpin' Jupiter (2020)(Quantum Sheep).tap", 40858, 0x34a1f400, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specjumpjupiter, Specjumpjupiter, Spec128)
STD_ROM_FN(Specjumpjupiter)

struct BurnDriver BurnSpecjumpjupiter = {
	"spec_jumpjupiter", NULL, "spec_spec128", NULL, "2020",
	"Jumpin' Jupiter (HB)\0", NULL, "Quantum Sheep", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 2, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecjumpjupiterRomInfo, SpecjumpjupiterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lost Treasures of Tulum, the (HB)

static struct BurnRomInfo SpeclostulumRomDesc[] = {
	{ "Lost Treasures of Tulum, the (2020)(RetroWorks).tap", 75999, 0xc64c38ad, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclostulum, Speclostulum, Spec128)
STD_ROM_FN(Speclostulum)

struct BurnDriver BurnSpeclostulum = {
	"spec_lostulum", NULL, "spec_spec128", NULL, "2020",
	"Lost Treasures of Tulum, the (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeclostulumRomInfo, SpeclostulumRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Misifu Remeow Color (HB)

static struct BurnRomInfo SpecmisifucolorRomDesc[] = {
	{ "misifu_COLOR(2021)(JSM Soft).tap",  73610, 0x387dd30a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmisifucolor, Specmisifucolor, Spec128)
STD_ROM_FN(Specmisifucolor)

struct BurnDriver BurnSpecmisifucolor = {
	"spec_misifucolor", NULL, "spec_spec128", NULL, "2021",
	"Misifu Remeow Color (HB, v1.4.1)\0", "Alley Cat demake for ZX Spectrum", "JSM Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecmisifucolorRomInfo, SpecmisifucolorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3

};

// Misifu Remeow CGA (HB)

static struct BurnRomInfo SpecmisifucgaRomDesc[] = {
	{ "misifu_CGA(2021)(JSM Soft).tap", 73469, 0x84550b57, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmisifucga, Specmisifucga, Spec128)
STD_ROM_FN(Specmisifucga)

struct BurnDriver BurnSpecmisifucga = {
	"spec_misifucga", "spec_misifucolor", "spec_spec128", NULL, "2021",
	"Misifu Remeow CGA (HB, v1.4.1)\0", "Alley Cat demake for ZX Spectrum", "JSM Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecmisifucgaRomInfo, SpecmisifucgaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// MultiDude (HB)

static struct BurnRomInfo SpecmultidudeRomDesc[] = {
	{ "MultiDude128k (2014)(RetroSouls).tap",  47634, 0x0892872f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmultidude, Specmultidude, Spec128)
STD_ROM_FN(Specmultidude)

struct BurnDriver BurnSpecmultidude = {
	"spec_multidude", NULL, "spec_spec128", NULL, "2014",
	"MultiDude (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecmultidudeRomInfo, SpecmultidudeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Old Tower (HB)

static struct BurnRomInfo SpecoldtowerRomDesc[] = {
	{ "OldTower128k (2018)(RetroSouls).tap",  97764, 0x837df93f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specoldtower, Specoldtower, Spec128)
STD_ROM_FN(Specoldtower)

struct BurnDriver BurnSpecoldtower = {
	"spec_oldtower", NULL, "spec_spec128", NULL, "2019",
	"Old Tower (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecoldtowerRomInfo, SpecoldtowerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Relic, the (HB)

static struct BurnRomInfo SpecrelicRomDesc[] = {
	{ "The Relic 128K (2020)(Roolandoo).tap",  46629, 0x183c5158, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrelic, Specrelic, Spec128)
STD_ROM_FN(Specrelic)

struct BurnDriver BurnSpecrelic = {
	"spec_relic", "spec_lareliquia", "spec_spec128", NULL, "2020",
	"Relic, the (HB)\0", NULL, "Roolandoo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecrelicRomInfo, SpecrelicRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sir Ababol 2 (HB)

static struct BurnRomInfo Specsirababol2RomDesc[] = {
	{ "Sir Ababol 2 128k (2013)(mojon twins).tap", 79038, 0x61a75108, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsirababol2, Specsirababol2, Spec128)
STD_ROM_FN(Specsirababol2)

struct BurnDriver BurnSpecsirababol2 = {
	"spec_sirababol2", NULL, "spec_spec128", NULL, "2013",
	"Sir Ababol 2 (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specsirababol2RomInfo, Specsirababol2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sir Ababol DX (HB)

static struct BurnRomInfo SpecsirababoldxRomDesc[] = {
	{ "Sir Ababol DX (2013)(mojon twins).tap", 43469, 0x06705a38, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsirababoldx, Specsirababoldx, Spec128)
STD_ROM_FN(Specsirababoldx)

struct BurnDriver BurnSpecsirababoldx = {
	"spec_sirababoldx", NULL, "spec_spec128", NULL, "2013",
	"Sir Ababol DX (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecsirababoldxRomInfo, SpecsirababoldxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// That Sinking Feeling (HB)

static struct BurnRomInfo SpectsfeelingRomDesc[] = {
	{ "That Sinking Feeling(2017)(Sunteam).tap", 34567, 0xe7822c33, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectsfeeling, Spectsfeeling, Spec128)
STD_ROM_FN(Spectsfeeling)

struct BurnDriver BurnSpectsfeeling = {
	"spec_tsfeeling", NULL, "spec_spec128", NULL, "2017",
	"That Sinking Feeling (HB)\0", NULL, "Sunteam", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpectsfeelingRomInfo, SpectsfeelingRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Witch v1.0.1 (HB)

static struct BurnRomInfo SpecthewitchRomDesc[] = {
	{ "The Witch (v1.0.1)(2020)(Serranito Online).tap",  48970, 0x4dacc7d8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specthewitch, Specthewitch, Spec128)
STD_ROM_FN(Specthewitch)

struct BurnDriver BurnSpecthewitch = {
	"spec_thewitch", NULL, "spec_spec128", NULL, "2020",
	"The Witch (HB, v1.0.1)\0", "The remake of classic game Cauldron", "Serranito Online", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT | GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecthewitchRomInfo, SpecthewitchRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3

};

// The Witch ending english version (HB)

static struct BurnRomInfo SpecthewitchenRomDesc[] = {
	{ "The Witch (ending EN)(2020)(Serranito Online).tap", 36161, 0x1d18f64a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specthewitchen, Specthewitchen, Spec128)
STD_ROM_FN(Specthewitchen)

struct BurnDriver BurnSpecthewitchen = {
	"spec_thewitchen", "spec_thewitch", "spec_spec128", NULL, "2020",
	"The Witch ending eng-version (HB, v1.0.1)\0", "The remake of classic game Cauldron", "Serranito Online", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT | GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecthewitchenRomInfo, SpecthewitchenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Witch ending espanish version (HB)

static struct BurnRomInfo SpecthewitchesRomDesc[] = {
	{ "The Witch (ending ES)(2020)(Serranito Online).tap", 36165, 0x3c916134, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specthewitches, Specthewitches, Spec128)
STD_ROM_FN(Specthewitches)

struct BurnDriver BurnSpecthewitches = {
	"spec_thewitches", "spec_thewitch", "spec_spec128", NULL, "2020",
	"The Witch ending esp-version (HB, v1.0.1)\0", "The remake of classic game Cauldron", "Serranito Online", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT | GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecthewitchesRomInfo, SpecthewitchesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tourmaline (HB)

static struct BurnRomInfo SpectrmlineRomDesc[] = {
	{ "Tourmaline (2016)(RetroSouls).tap",  46613, 0xf573c97f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectrmline, Spectrmline, Spec128)
STD_ROM_FN(Spectrmline)

struct BurnDriver BurnSpectrmline = {
	"spec_trmline", NULL, "spec_spec128", NULL, "2016",
	"Tourmaline (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpectrmlineRomInfo, SpectrmlineRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yazzie (HB)

static struct BurnRomInfo SpecyazzieRomDesc[] = {
	{ "Yazzie128k (2019)(RetroSouls).tap",  70103, 0x68d21e49, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specyazzie, Specyazzie, Spec128)
STD_ROM_FN(Specyazzie)

struct BurnDriver BurnSpecyazzie = {
	"spec_yazzie", NULL, "spec_spec128", NULL, "2019",
	"Yazzie (HB)\0", NULL, "RetroSouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecyazzieRomInfo, SpecyazzieRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Willy The Wasp (HB)

static struct BurnRomInfo SpecWwaspRomDesc[] = {
	{ "Willy The Wasp (2014)(The Death Squad).tap", 41193, 0x030e8442, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWwasp, SpecWwasp, Spec128)
STD_ROM_FN(SpecWwasp)

struct BurnDriver BurnSpecWwasp = {
	"spec_wwasp", NULL, "spec_spec128", NULL, "2014",
	"Willy The Wasp (HB)\0", NULL, "The Death Squad", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecWwaspRomInfo, SpecWwaspRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Willy The Wasp 2 (HB)

static struct BurnRomInfo SpecWwasp2RomDesc[] = {
	{ "Willy The Wasp 2 (2014)(The Death Squad).tap", 64604, 0x3e8b70fb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWwasp2, SpecWwasp2, Spec128)
STD_ROM_FN(SpecWwasp2)

struct BurnDriver BurnSpecWwasp2 = {
	"spec_wwasp2", NULL, "spec_spec128", NULL, "2014",
	"Willy The Wasp 2 (HB)\0", NULL, "The Death Squad", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecWwasp2RomInfo, SpecWwasp2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Black & White v1.1 (HB)

static struct BurnRomInfo SpecbnwRomDesc[] = {
	{ "Black & White v1.1 (2020)(Pat Morita Team).tap", 72706, 0x6629deb1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specbnw, Specbnw, Spec128)
STD_ROM_FN(Specbnw)

struct BurnDriver BurnSpecbnw = {
	"spec_bnw", NULL, "spec_spec128", NULL, "2020",
	"Black & White (HB, v1.1)\0", NULL, "Pat Morita Team", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecbnwRomInfo, SpecbnwRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Circuitry 48k  v1.1 (HB)

static struct BurnRomInfo SpeccircuitryRomDesc[] = {
	{ "Circuitry48k v1.1 (2017)(Rucksack Games).tap", 31538, 0xc97061d1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccircuitry, Speccircuitry, Spec128)
STD_ROM_FN(Speccircuitry)

struct BurnDriver BurnSpeccircuitry = {
	"spec_circuitry", NULL, "spec_spec128", NULL, "2017",
	"Circuitry (HB, v1.1)\0", NULL, "Rucksack Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeccircuitryRomInfo, SpeccircuitryRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Coloco (HB)

static struct BurnRomInfo SpeccolocoRomDesc[] = {
	{ "Coloco (2020)(Mojon Twins).tap", 46855, 0xdd07fa17, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccoloco, Speccoloco, Spec128)
STD_ROM_FN(Speccoloco)

struct BurnDriver BurnSpeccoloco = {
	"spec_coloco", NULL, "spec_spec128", NULL, "2020",
	"Coloco (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeccolocoRomInfo, SpeccolocoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Foggy's Quest 128k (HB)

static struct BurnRomInfo SpecfoggyquestRomDesc[] = {
	{ "Foggy's Quest128k (2017)(Rucksack Games).tap", 43528, 0x0d624a1c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specfoggyquest, Specfoggyquest, Spec128)
STD_ROM_FN(Specfoggyquest)

struct BurnDriver BurnSpecfoggyquest = {
	"spec_foggyquest", NULL, "spec_spec128", NULL, "2017",
	"Foggy's Quest (HB)\0", NULL, "Rucksack Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecfoggyquestRomInfo, SpecfoggyquestRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// All Hallows Rise of Pumpkin (HB)

static struct BurnRomInfo SpechlwpumpkinRomDesc[] = {
	{ "ALL Hallows Rise of Pumpkin (2018)(Rucksack Games).tap", 46999, 0x76ecb3a0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spechlwpumpkin, Spechlwpumpkin, Spec128)
STD_ROM_FN(Spechlwpumpkin)

struct BurnDriver BurnSpechlwpumpkin = {
	"spec_hlwpumpkin", NULL, "spec_spec128", NULL, "2018",
	"All Hallows Rise of Pumpkin (HB)\0", NULL, "Rucksack Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpechlwpumpkinRomInfo, SpechlwpumpkinRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninjajar v1.1 (HB)

static struct BurnRomInfo SpecninjajarRomDesc[] = {
	{ "Ninjajar v1.1 (2014)(Mojon Twins).tap", 122675, 0x617db06c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specninjajar, Specninjajar, Spec128)
STD_ROM_FN(Specninjajar)

struct BurnDriver BurnSpecninjajar = {
	"spec_ninjajar", NULL, "spec_spec128", NULL, "2014",
	"Ninjajar (HB, v1.1)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecninjajarRomInfo, SpecninjajarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rubicon v1.1 (HB)

static struct BurnRomInfo SpecrubiconRomDesc[] = {
	{ "Rubicon v1.1 (2018)(Rucksack Games).tap", 48224, 0x99ec517e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrubicon, Specrubicon, Spec128)
STD_ROM_FN(Specrubicon)

struct BurnDriver BurnSpecrubicon = {
	"spec_rubicon", NULL, "spec_spec128", NULL, "2018",
	"Rubicon (HB, v1.1)\0", NULL, "Rucksack Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecrubiconRomInfo, SpecrubiconRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sector Invasion (HB)

static struct BurnRomInfo SpecsectinvnRomDesc[] = {
	{ "Sector Invasion (2014).tap", 38873, 0x275c56d5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsectinvn, Specsectinvn, Spec128)
STD_ROM_FN(Specsectinvn)

struct BurnDriver BurnSpecsectinvn = {
	"spec_sectinvn", NULL, "spec_spec128", NULL, "2014",
	"Sector Invasion (HB)\0", NULL, "Retrosouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecsectinvnRomInfo, SpecsectinvnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Tenebra Macabre (HB)

static struct BurnRomInfo SpectmacabreRomDesc[] = {
	{ "Tenebra Macabre (2013)(Mojon Twins).tap", 39490, 0xbfa58b2c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Spectmacabre, Spectmacabre, Spec128)
STD_ROM_FN(Spectmacabre)

struct BurnDriver BurnSpectmacabre = {
	"spec_tmacabre", NULL, "spec_spec128", NULL, "2013",
	"Tenebra Macabre (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpectmacabreRomInfo, SpectmacabreRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vampire Vengeance v1.1 (HB)

static struct BurnRomInfo SpecvampvengRomDesc[] = {
	{ "Vampire Vengeance v1.1 (2020)(Poe Games).tap", 58059, 0x2796871d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specvampveng, Specvampveng, Spec128)
STD_ROM_FN(Specvampveng)

struct BurnDriver BurnSpecvampveng = {
	"spec_vampveng", NULL, "spec_spec128", NULL, "2020",
	"Vampire Vengeance (HB, v1.1)\0", NULL, "Poe Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecvampvengRomInfo, SpecvampvengRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cray-5 (HB)

static struct BurnRomInfo SpecCray5RomDesc[] = {
	{ "Cray-5 (2011)(Retroworks).tap", 53875, 0xa1467bf5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCray5, SpecCray5, Spec128)
STD_ROM_FN(SpecCray5)

struct BurnDriver BurnSpecCray5 = {
	"spec_cray5", NULL, "spec_spec128", NULL, "2011",
	"Cray-5 (HB)\0", NULL, "Retroworks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecCray5RomInfo, SpecCray5RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dead Flesh Boy (HB)

static struct BurnRomInfo SpecDfboyRomDesc[] = {
	{ "Dead Flesh Boy (2015)(VANB).tap", 40669, 0x3375d2c9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDfboy, SpecDfboy, Spec128)
STD_ROM_FN(SpecDfboy)

struct BurnDriver BurnSpecDfboy = {
	"spec_dfboy", NULL, "spec_spec128", NULL, "2015",
	"Dead Flesh Boy (HB)\0", NULL, "VANB", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecDfboyRomInfo, SpecDfboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Flype (HB)

static struct BurnRomInfo SpecFlypeRomDesc[] = {
	{ "Flype (2015)(Repixel8).tap", 41719, 0x2b9a90f2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFlype, SpecFlype, Spec128)
STD_ROM_FN(SpecFlype)

struct BurnDriver BurnSpecFlype = {
	"spec_flype", NULL, "spec_spec128", NULL, "2015",
	"Flype (HB)\0", NULL, "Repixel8", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecFlypeRomInfo, SpecFlypeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Horace and the Robots (HB)

static struct BurnRomInfo SpecHoracerobotsRomDesc[] = {
	{ "Horace and the Robots (2017)(Steve Snake).tap", 25517, 0x05fd13e2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHoracerobots, SpecHoracerobots, Spec128)
STD_ROM_FN(SpecHoracerobots)

struct BurnDriver BurnSpecHoracerobots = {
	"spec_horacerobots", NULL, "spec_spec128", NULL, "2017",
	"Horace and the Robots (HB)\0", NULL, "Steve Snake", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecHoracerobotsRomInfo, SpecHoracerobotsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ooze (HB)

static struct BurnRomInfo SpecOozeRomDesc[] = {
	{ "Ooze (2017)(Bubblesoft).tap", 49790, 0x188d3e4e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOoze, SpecOoze, Spec128)
STD_ROM_FN(SpecOoze)

struct BurnDriver BurnSpecOoze = {
	"spec_ooze", NULL, "spec_spec128", NULL, "2017",
	"Ooze (HB)\0", NULL, "Bubblesoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecOozeRomInfo, SpecOozeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Restless Andre (HB)

static struct BurnRomInfo SpecRestlessandreRomDesc[] = {
	{ "Restless Andre (2019)(Jaime Grilo).tap", 48113, 0x0db7023a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRestlessandre, SpecRestlessandre, Spec128)
STD_ROM_FN(SpecRestlessandre)

struct BurnDriver BurnSpecRestlessandre = {
	"spec_restlessandre", NULL, "spec_spec128", NULL, "2019",
	"Restless Andre (HB)\0", NULL, "Jaime Grilo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecRestlessandreRomInfo, SpecRestlessandreRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Terrahawks (HB)

static struct BurnRomInfo SpecTerrahawksRomDesc[] = {
	{ "Terrahawks (2014)(Gary James).tap", 24138, 0xd6f05303, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTerrahawks, SpecTerrahawks, Spec128)
STD_ROM_FN(SpecTerrahawks)

struct BurnDriver BurnSpecTerrahawks = {
	"spec_terrahawks", NULL, "spec_spec128", NULL, "2014",
	"Terrahawks (HB)\0", NULL, "Gary James", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecTerrahawksRomInfo, SpecTerrahawksRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Incredible Shrinking Professor, the  (HB)

static struct BurnRomInfo SpecincprofRomDesc[] = {
	{ "Incredible Shrinking Professor, the (2017)(Rucksack Games).tap", 44116, 0x03e3de19, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specincprof, Specincprof, Spectrum)
STD_ROM_FN(Specincprof)

struct BurnDriver BurnSpecincprof = {
	"spec_incprof", NULL, "spec_spectrum", NULL, "2017",
	"Incredible Shrinking Professor, the (HB)\0", NULL, "Rucksack Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecincprofRomInfo, SpecincprofRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Robots Rumble  (HB)

static struct BurnRomInfo SpecrrumbleRomDesc[] = {
	{ "Robots Rumble (2018)(Miguetelo).tap", 39461, 0xc7c5dc79, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specrrumble, Specrrumble, Spectrum)
STD_ROM_FN(Specrrumble)

struct BurnDriver BurnSpecrrumble = {
	"spec_rrumble", NULL, "spec_spectrum", NULL, "2018",
	"Robots Rumble (HB)\0", NULL, "Miguetelo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecrrumbleRomInfo, SpecrrumbleRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bobby Carrot (HB)

static struct BurnRomInfo SpecBobbycarrotRomDesc[] = {
	{ "Bobby Carrot (2018)(Diver, Quiet, Kyv, Zorba).tap", 34125, 0xaf5919ca, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBobbycarrot, SpecBobbycarrot, Spec128)
STD_ROM_FN(SpecBobbycarrot)

struct BurnDriver BurnSpecBobbycarrot = {
	"spec_bobbycarrot", NULL, "spec_spec128", NULL, "2018",
	"Bobby Carrot (HB)\0", NULL, "Diver, Quiet, Kyv, Zorba", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecBobbycarrotRomInfo, SpecBobbycarrotRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// CastleCapers (HB)

static struct BurnRomInfo SpecCastlecapersRomDesc[] = {
	{ "CastleCapers (2017)(Gabriele Amore).tap", 36751, 0xdb772c7c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCastlecapers, SpecCastlecapers, Spec128)
STD_ROM_FN(SpecCastlecapers)

struct BurnDriver BurnSpecCastlecapers = {
	"spec_castlecapers", NULL, "spec_spec128", NULL, "2017",
	"CastleCapers (HB)\0", NULL, "Gabriele Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecCastlecapersRomInfo, SpecCastlecapersRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dirty Dozer (HB)

static struct BurnRomInfo SpecDirtydozerRomDesc[] = {
	{ "Dirty Dozer (2019)(Miguetelo).tap", 41391, 0xaf06dd37, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDirtydozer, SpecDirtydozer, Spec128)
STD_ROM_FN(SpecDirtydozer)

struct BurnDriver BurnSpecDirtydozer = {
	"spec_dirtydozer", NULL, "spec_spec128", NULL, "2019",
	"Dirty Dozer (HB)\0", NULL, "Miguetelo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecDirtydozerRomInfo, SpecDirtydozerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// ElStompo (HB)

static struct BurnRomInfo SpecElstompoRomDesc[] = {
	{ "ElStompo (2014)(Stonechat Productions).tap", 44449, 0xb2370ea9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecElstompo, SpecElstompo, Spec128)
STD_ROM_FN(SpecElstompo)

struct BurnDriver BurnSpecElstompo = {
	"spec_elstompo", NULL, "spec_spec128", NULL, "2014",
	"ElStompo (HB)\0", NULL, "Stonechat Productions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecElstompoRomInfo, SpecElstompoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gandalf Deluxe (HB)

static struct BurnRomInfo SpecGandalfRomDesc[] = {
	{ "Gandalf Deluxe (2018)(Speccy Nights).tap", 95019, 0x1f7f26c8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGandalf, SpecGandalf, Spec128)
STD_ROM_FN(SpecGandalf)

struct BurnDriver BurnSpecGandalf = {
	"spec_gandalf", NULL, "spec_spec128", NULL, "2018",
	"Gandalf Deluxe (HB)\0", NULL, "Speccy Nights", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecGandalfRomInfo, SpecGandalfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Metal Man (HB, russian)

static struct BurnRomInfo SpecMetalmanRomDesc[] = {
	{ "Metal Man (2014)(Oleg Origin).tap", 48298, 0xef74d54b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMetalman, SpecMetalman, Spec128)
STD_ROM_FN(SpecMetalman)

struct BurnDriver BurnSpecMetalman = {
	"spec_metalman", NULL, "spec_spec128", NULL, "2014",
	"Metal Man (HB, russian)\0", NULL, "Oleg Origin", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecMetalmanRomInfo, SpecMetalmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mighty Final Fight (HB)

static struct BurnRomInfo SpecMightyffRomDesc[] = {
	{ "Mighty Final Fight (2018)(SaNchez).tap", 302096, 0x1697a6ed, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMightyff, SpecMightyff, Spec128)
STD_ROM_FN(SpecMightyff)

struct BurnDriver BurnSpecMightyff = {
	"spec_mightyff", NULL, "spec_spec128", NULL, "2018",
	"Mighty Final Fight (HB)\0", NULL, "SaNchez", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecMightyffRomInfo, SpecMightyffRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninja Gaiden Shadow Warriors (HB)

static struct BurnRomInfo SpecNinjagaidenRomDesc[] = {
	{ "Ninja Gaiden Shadow Warriors (2018)(Jerri, DaRkHoRaCe, Diver).tap", 103645, 0x44081e87, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNinjagaiden, SpecNinjagaiden, Spec128)
STD_ROM_FN(SpecNinjagaiden)

struct BurnDriver BurnSpecNinjagaiden = {
	"spec_ninjagaiden", NULL, "spec_spec128", NULL, "2018",
	"Ninja Gaiden Shadow Warriors (HB)\0", NULL, "Jerri, DaRkHoRaCe, Diver", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SCRFIGHT, 0,
	SpectrumGetZipName, SpecNinjagaidenRomInfo, SpecNinjagaidenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Parachute (HB)

static struct BurnRomInfo SpecParachuteRomDesc[] = {
	{ "Parachute (2018)(Miguetelo).tap", 40791, 0xd23bd0ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecParachute, SpecParachute, Spec128)
STD_ROM_FN(SpecParachute)

struct BurnDriver BurnSpecParachute = {
	"spec_parachute", NULL, "spec_spec128", NULL, "2018",
	"Parachute (HB)\0", NULL, "Miguetelo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecParachuteRomInfo, SpecParachuteRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Parsec (HB)

static struct BurnRomInfo SpecParsecRomDesc[] = {
	{ "Parsec (2020)(Martin Mangan).tap", 40874, 0xa61d638d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecParsec, SpecParsec, Spec128)
STD_ROM_FN(SpecParsec)

struct BurnDriver BurnSpecParsec = {
	"spec_parsec", NULL, "spec_spec128", NULL, "2020",
	"Parsec (HB)\0", NULL, "Martin Mangan", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecParsecRomInfo, SpecParsecRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Redshift (HB)

static struct BurnRomInfo SpecRedshiftRomDesc[] = {
	{ "Redshift (2019)(Ariel Ruiz).tap", 79855, 0x2198fb31, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRedshift, SpecRedshift, Spec128)
STD_ROM_FN(SpecRedshift)

struct BurnDriver BurnSpecRedshift = {
	"spec_redshift", NULL, "spec_spec128", NULL, "2019",
	"Redshift (HB)\0", NULL, "Ariel Ruiz", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecRedshiftRomInfo, SpecRedshiftRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Space Junk (HB)

static struct BurnRomInfo SpecSpacejunkRomDesc[] = {
	{ "Space Junk (2017)(Miguetelo).tap", 42761, 0xaa8930bf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSpacejunk, SpecSpacejunk, Spec128)
STD_ROM_FN(SpecSpacejunk)

struct BurnDriver BurnSpecSpacejunk = {
	"spec_spacejunk", NULL, "spec_spec128", NULL, "2017",
	"Space Junk (HB)\0", NULL, "Miguetelo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecSpacejunkRomInfo, SpecSpacejunkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Squares (HB)

static struct BurnRomInfo SpecSquaresRomDesc[] = {
	{ "Squares (2017)(Kas29).tap", 16053, 0x1b924ae6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSquares, SpecSquares, Spec128)
STD_ROM_FN(SpecSquares)

struct BurnDriver BurnSpecSquares = {
	"spec_squares", NULL, "spec_spec128", NULL, "2017",
	"Squares (HB)\0", NULL, "Kas29", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecSquaresRomInfo, SpecSquaresRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stormfinch (HB)

static struct BurnRomInfo SpecStormfinchRomDesc[] = {
	{ "Stormfinch (2015)(Stonechat).tap", 47695, 0xcc53ba30, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStormfinch, SpecStormfinch, Spec128)
STD_ROM_FN(SpecStormfinch)

struct BurnDriver BurnSpecStormfinch = {
	"spec_stormfinch", NULL, "spec_spec128", NULL, "2015",
	"Stormfinch (HB)\0", NULL, "Stonechat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecStormfinchRomInfo, SpecStormfinchRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sword Of Ilana (HB)

static struct BurnRomInfo SpecSwordofilanaRomDesc[] = {
	{ "Sword Of Ilana (2017)(RetroWorks).tap", 212617, 0x448410cc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSwordofilana, SpecSwordofilana, Spec128)
STD_ROM_FN(SpecSwordofilana)

struct BurnDriver BurnSpecSwordofilana = {
	"spec_swordofilana", NULL, "spec_spec128", NULL, "2017",
	"Sword Of Ilana (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecSwordofilanaRomInfo, SpecSwordofilanaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lala Prologue (HB)

static struct BurnRomInfo SpeclalaRomDesc[] = {
	{ "Lala Prologue (2010)(Mojon Twins).tap", 32855, 0x547b6601, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speclala, Speclala, Spec128)
STD_ROM_FN(Speclala)

struct BurnDriver BurnSpeclala = {
	"spec_lala", NULL, "spec_spec128", NULL, "2010",
	"Lala Prologue (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpeclalaRomInfo, SpeclalaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sgt. Helmet Zero (HB)

static struct BurnRomInfo SpecsgthelmetRomDesc[] = {
	{ "Sgt. Helmet Zero (2009)(Mojon Twins).tap", 62412, 0xd182aa2e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsgthelmet, Specsgthelmet, Spec128)
STD_ROM_FN(Specsgthelmet)

struct BurnDriver BurnSpecsgthelmet = {
	"spec_sgthelmet", NULL, "spec_spec128", NULL, "2009",
	"Sgt. Helmet Zero (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecsgthelmetRomInfo, SpecsgthelmetRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Subaquatic Reloaded (HB)

static struct BurnRomInfo SpecsubacquaticRomDesc[] = {
	{ "Subaquatic Reloaded (2009)(Mojon Twins).tap", 55547, 0x45815dd3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsubacquatic, Specsubacquatic, Spec128)
STD_ROM_FN(Specsubacquatic)

struct BurnDriver BurnSpecsubacquatic = {
	"spec_subacquatic", NULL, "spec_spec128", NULL, "2010",
	"Subaquatic Reloaded (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecsubacquaticRomInfo, SpecsubacquaticRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Uwol (HB)

static struct BurnRomInfo SpecuwolRomDesc[] = {
	{ "Uwol (2010)(Mojon Twins).tap", 50254, 0x4fee82ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specuwol, Specuwol, Spec128)
STD_ROM_FN(Specuwol)

struct BurnDriver BurnSpecuwol = {
	"spec_uwol", NULL, "spec_spec128", NULL, "2009",
	"Uwol (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecuwolRomInfo, SpecuwolRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Zombie Calavera Prologue (HB)

static struct BurnRomInfo SpeczcalaveraRomDesc[] = {
	{ "Zombie Calavera Prologue (2010)(Mojon Twins).tap", 37966, 0x9eaf2b79, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speczcalavera, Speczcalavera, Spec128)
STD_ROM_FN(Speczcalavera)

struct BurnDriver BurnSpeczcalavera = {
	"spec_zcalavera", NULL, "spec_spec128", NULL, "2010",
	"Zombie Calavera Prologue (HB)\0", NULL, "Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpeczcalaveraRomInfo, SpeczcalaveraRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// AstroCop (HB)

static struct BurnRomInfo SpecAstrocopRomDesc[] = {
	{ "AstroCop (2020)(MK1).tap", 41005, 0x2c1e9ca5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAstrocop, SpecAstrocop, Spec128)
STD_ROM_FN(SpecAstrocop)

struct BurnDriver BurnSpecAstrocop = {
	"spec_astrocop", NULL, "spec_spec128", NULL, "2020",
	"AstroCop (HB)\0", NULL, "MK1", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAstrocopRomInfo, SpecAstrocopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Herbert the Turbot (HB)

static struct BurnRomInfo SpecHerberttbRomDesc[] = {
	{ "Herbert the Turbot (2010)(Bob Smith).z80", 81123, 0x0a9c0799, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHerberttb, SpecHerberttb, Spec128)
STD_ROM_FN(SpecHerberttb)

struct BurnDriver BurnSpecHerberttb = {
	"spec_herberttb", NULL, "spec_spec128", NULL, "2010",
	"Herbert the Turbot (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecHerberttbRomInfo, SpecHerberttbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jetpac RX (HB)

static struct BurnRomInfo SpecJetpacrxRomDesc[] = {
	{ "JetpacRX (2020)(highriser).tap", 22814, 0x2f0a86ff, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetpacrx, SpecJetpacrx, Spec128)
STD_ROM_FN(SpecJetpacrx)

struct BurnDriver BurnSpecJetpacrx = {
	"spec_jetpacrx", "spec_jetpac", "spec_spec128", NULL, "2020",
	"Jetpac RX (HB)\0", "RX Mod of classic Jetpac", "highriser", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecJetpacrxRomInfo, SpecJetpacrxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// La Reliquia (HB)

static struct BurnRomInfo SpecLareliquiaRomDesc[] = {
	{ "La Reliquia (2020)(Angel Colaso).tap", 46629, 0x183c5158, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLareliquia, SpecLareliquia, Spec128)
STD_ROM_FN(SpecLareliquia)

struct BurnDriver BurnSpecLareliquia = {
	"spec_lareliquia", NULL, "spec_spec128", NULL, "2020",
	"La Reliquia (HB)\0", NULL, "Angel Colaso", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecLareliquiaRomInfo, SpecLareliquiaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rygar 2020 (HB)

static struct BurnRomInfo SpecRygar2020RomDesc[] = {
	{ "Rygar 2020 (2020)(Ralf).tap", 35648, 0xc0af87aa, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRygar2020, SpecRygar2020, Spec128)
STD_ROM_FN(SpecRygar2020)

struct BurnDriver BurnSpecRygar2020 = {
	"spec_rygar2020", NULL, "spec_spec128", NULL, "2020",
	"Rygar 2020 (HB)\0", NULL, "Ralf", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecRygar2020RomInfo, SpecRygar2020RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sophia (HB)

static struct BurnRomInfo SpecsophiaRomDesc[] = {
	{ "Sophia (2017)(Alessandro Grussu).tap", 86193, 0x3681c3a0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsophia, Specsophia, Spec128)
STD_ROM_FN(Specsophia)

struct BurnDriver BurnSpecsophia = {
	"spec_sophia", NULL, "spec_spec128", NULL, "2017",
	"Sophia (HB)\0", NULL, "Alessandro Grussu", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecsophiaRomInfo, SpecsophiaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sophia II (HB)

static struct BurnRomInfo Specsophia2RomDesc[] = {
	{ "Sophia II (2019)(Alessandro Grussu).tap", 78009, 0xea86b2a1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsophia2, Specsophia2, Spec128)
STD_ROM_FN(Specsophia2)

struct BurnDriver BurnSpecsophia2 = {
	"spec_sophia2", NULL, "spec_spec128", NULL, "2019",
	"Sophia II (HB)\0", NULL, "Alessandro Grussu", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, Specsophia2RomInfo, Specsophia2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Apulija-13 (HB, v2.01)

static struct BurnRomInfo SpecApulija13RomDesc[] = {
	{ "Apulija-13 (2013)(Alessandro Grussu).tap", 28214, 0x356db043, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecApulija13, SpecApulija13, Spec128)
STD_ROM_FN(SpecApulija13)

struct BurnDriver BurnSpecApulija13 = {
	"spec_apulija13", NULL, "spec_spec128", NULL, "2018",
	"Apulija-13 (HB, v2.01)\0", NULL, "Alessandro Grussu", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_ADV, 0,
	SpectrumGetZipName, SpecApulija13RomInfo, SpecApulija13RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Astronaut Labyrinth (HB)

static struct BurnRomInfo SpecAstrolabyRomDesc[] = {
	{ "Astronaut Labyrinth (2018)(Jaime Grilo).tap", 44602, 0x984c2c8a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAstrolaby, SpecAstrolaby, Spec128)
STD_ROM_FN(SpecAstrolaby)

struct BurnDriver BurnSpecAstrolaby = {
	"spec_astrolaby", NULL, "spec_spec128", NULL, "2018",
	"Astronaut Labyrinth (HB)\0", NULL, "Jaime Grilo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAstrolabyRomInfo, SpecAstrolabyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bean Brothers (HB)

static struct BurnRomInfo SpecBeanbrosRomDesc[] = {
	{ "Bean Brothers (2018)(Stonechat Productions).tap", 55722, 0xaf989916, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBeanbros, SpecBeanbros, Spec128)
STD_ROM_FN(SpecBeanbros)

struct BurnDriver BurnSpecBeanbros = {
	"spec_beanbros", NULL, "spec_spec128", NULL, "2018",
	"Bean Brothers (HB)\0", NULL, "Stonechat Productions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecBeanbrosRomInfo, SpecBeanbrosRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Castle Of Sorrow

static struct BurnRomInfo SpecCastleofsorrowRomDesc[] = {
	{ "Castle Of Sorrow (2018)(ZXMan48k).tap", 42259, 0x7750645b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCastleofsorrow, SpecCastleofsorrow, Spec128)
STD_ROM_FN(SpecCastleofsorrow)

struct BurnDriver BurnSpecCastleofsorrow = {
	"spec_castleofsorrow", NULL, "spec_spec128", NULL, "2018",
	"Castle Of Sorrow (HB)\0", NULL, "ZXMan48k", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCastleofsorrowRomInfo, SpecCastleofsorrowRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dungeon Raiders

static struct BurnRomInfo SpecDungeonraidersRomDesc[] = {
	{ "Dungeon Raiders (2018)(Payndz).tap", 33635, 0x9bfd967b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDungeonraiders, SpecDungeonraiders, Spec128)
STD_ROM_FN(SpecDungeonraiders)

struct BurnDriver BurnSpecDungeonraiders = {
	"spec_dungeonraiders", NULL, "spec_spec128", NULL, "2018",
	"Dungeon Raiders (HB)\0", NULL, "Payndz", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDungeonraidersRomInfo, SpecDungeonraidersRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Eggsterminator

static struct BurnRomInfo SpecEggsterminatorRomDesc[] = {
	{ "The Eggsterminator (2018)(The Death Squad).tap", 48022, 0x6960fff0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEggsterminator, SpecEggsterminator, Spec128)
STD_ROM_FN(SpecEggsterminator)

struct BurnDriver BurnSpecEggsterminator = {
	"spec_eggsterminator", NULL, "spec_spec128", NULL, "2018",
	"The Eggsterminator (HB)\0", NULL, "The Death Squad", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecEggsterminatorRomInfo, SpecEggsterminatorRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Impossabubble

static struct BurnRomInfo SpecImpossabubbleRomDesc[] = {
	{ "Impossabubble (2018)(Dave Clarke).tap", 46080, 0x7cd3626a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecImpossabubble, SpecImpossabubble, Spec128)
STD_ROM_FN(SpecImpossabubble)

struct BurnDriver BurnSpecImpossabubble = {
	"spec_impossabubble", NULL, "spec_spec128", NULL, "2018",
	"Impossabubble (HB)\0", NULL, "Dave Clarke", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecImpossabubbleRomInfo, SpecImpossabubbleRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mister Kung-Fu

static struct BurnRomInfo SpecMrkungfuRomDesc[] = {
	{ "Mister Kung-Fu (2018)(Uprising).tap", 49193, 0x5ed14eb8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMrkungfu, SpecMrkungfu, Spec128)
STD_ROM_FN(SpecMrkungfu)

struct BurnDriver BurnSpecMrkungfu = {
	"spec_mrkungfu", NULL, "spec_spec128", NULL, "2018",
	"Mister Kung-Fu (HB)\0", NULL, "Uprising", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMrkungfuRomInfo, SpecMrkungfuRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// OctuKtty

static struct BurnRomInfo SpecOctuKttyRomDesc[] = {
	{ "OctuKtty (2018)(UltraNarwhal).tap", 43800, 0x28b574a6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOctuKtty, SpecOctuKtty, Spec128)
STD_ROM_FN(SpecOctuKtty)

struct BurnDriver BurnSpecOctuKtty = {
	"spec_octuKtty", NULL, "spec_spec128", NULL, "2018",
	"OctuKtty (HB)\0", NULL, "UltraNarwhal", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecOctuKttyRomInfo, SpecOctuKttyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pooper Scooper

static struct BurnRomInfo SpecPooperscooperRomDesc[] = {
	{ "Pooper Scooper (2018)(The Death Squad).tap", 43438, 0x7bfc504c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPooperscooper, SpecPooperscooper, Spec128)
STD_ROM_FN(SpecPooperscooper)

struct BurnDriver BurnSpecPooperscooper = {
	"spec_pooperscooper", NULL, "spec_spec128", NULL, "2018",
	"Pooper Scooper (HB)\0", NULL, "The Death Squad", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPooperscooperRomInfo, SpecPooperscooperRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aliens Neoplasma (HB)

static struct BurnRomInfo SpecAliensneoRomDesc[] = {
	{ "Aliens Neoplasma (2019)(Sanchez Crew).tap", 64233, 0x6cf1a406, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAliensneo, SpecAliensneo, Spec128)
STD_ROM_FN(SpecAliensneo)

struct BurnDriver BurnSpecAliensneo = {
	"spec_aliensneo", NULL, "spec_spec128", NULL, "2019",
	"Aliens Neoplasma (HB)\0", NULL, "Sanchez Crew", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAliensneoRomInfo, SpecAliensneoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Booty the Remake (HB)

static struct BurnRomInfo SpecBootyremakeRomDesc[] = {
	{ "Booty the Remake (2019)(salvaKantero).tap", 65706, 0xaeaad13f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBootyremake, SpecBootyremake, Spec128)
STD_ROM_FN(SpecBootyremake)

struct BurnDriver BurnSpecBootyremake = {
	"spec_bootyremake", NULL, "spec_spec128", NULL, "2019",
	"Booty the Remake (HB)\0", NULL, "salvaKantero", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecBootyremakeRomInfo, SpecBootyremakeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bruce Lee RX

static struct BurnRomInfo SpecBruceleerxRomDesc[] = {
	{ "Bruce Lee RX (2019)(highriser).tap", 28795, 0x9dd8a157, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBruceleerx, SpecBruceleerx, Spec128)
STD_ROM_FN(SpecBruceleerx)

struct BurnDriver BurnSpecBruceleerx = {
	"spec_bruceleerx", NULL, "spec_spec128", NULL, "2019",
	"Bruce Lee RX (HB)\0", NULL, "highriser", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBruceleerxRomInfo, SpecBruceleerxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Car Wars

static struct BurnRomInfo SpecCarwarsRomDesc[] = {
	{ "Car Wars (2017)(Salva Cantero).tap", 73019, 0x6a59ae25, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCarwars, SpecCarwars, Spec128)
STD_ROM_FN(SpecCarwars)

struct BurnDriver BurnSpecCarwars = {
	"spec_carwars", NULL, "spec_spec128", NULL, "2017",
	"Car Wars (HB)\0", NULL, "Salva Cantero", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCarwarsRomInfo, SpecCarwarsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cocoa and the Time Machine

static struct BurnRomInfo SpecCocoatmRomDesc[] = {
	{ "Cocoa and the Time Machine (2020)(Minilop).tap", 46020, 0xbba37879, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCocoatm, SpecCocoatm, Spec128)
STD_ROM_FN(SpecCocoatm)

struct BurnDriver BurnSpecCocoatm = {
	"spec_cocoatm", NULL, "spec_spec128", NULL, "2020",
	"Cocoa and the Time Machine (HB)\0", NULL, "Minilop", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCocoatmRomInfo, SpecCocoatmRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Corey Coolbrew

static struct BurnRomInfo SpecCoreycbRomDesc[] = {
	{ "Corey Coolbrew (2020)(Blue Bilby).tap", 33670, 0x6f1f52e4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCoreycb, SpecCoreycb, Spec128)
STD_ROM_FN(SpecCoreycb)

struct BurnDriver BurnSpecCoreycb = {
	"spec_coreycb", NULL, "spec_spec128", NULL, "2020",
	"Corey Coolbrew (HB)\0", NULL, "Blue Bilby", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCoreycbRomInfo, SpecCoreycbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Curse of Trasmoz

static struct BurnRomInfo SpecCursetrasmozRomDesc[] = {
	{ "The Curse of Trasmoz (2020)(VolcanoBytes).tap", 48706, 0x8649a0b2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCursetrasmoz, SpecCursetrasmoz, Spec128)
STD_ROM_FN(SpecCursetrasmoz)

struct BurnDriver BurnSpecCursetrasmoz = {
	"spec_cursetrasmoz", NULL, "spec_spec128", NULL, "2020",
	"The Curse of Trasmoz (HB)\0", NULL, "VolcanoBytes", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCursetrasmozRomInfo, SpecCursetrasmozRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hyperkill

static struct BurnRomInfo SpecHyperkillRomDesc[] = {
	{ "Hyperkill (2017)(Mat Recardo).tap", 42502, 0x382ad799, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHyperkill, SpecHyperkill, Spec128)
STD_ROM_FN(SpecHyperkill)

struct BurnDriver BurnSpecHyperkill = {
	"spec_hyperkill", NULL, "spec_spec128", NULL, "2017",
	"Hyperkill (HB)\0", NULL, "Mat Recardo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecHyperkillRomInfo, SpecHyperkillRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jetpack Jock (HB)

static struct BurnRomInfo SpecJetpackjockRomDesc[] = {
	{ "Jetpack Jock (2020)(Gaz Marshall).tap", 24820, 0x028217db, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetpackjock, SpecJetpackjock, Spec128)
STD_ROM_FN(SpecJetpackjock)

struct BurnDriver BurnSpecJetpackjock = {
	"spec_jetpackjock", NULL, "spec_spec128", NULL, "2020",
	"Jetpack Jock (HB)\0", NULL, "Gaz Marshall", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecJetpackjockRomInfo, SpecJetpackjockRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mr. Do!

static struct BurnRomInfo SpecMrdoRomDesc[] = {
	{ "Mr. Do! (2019)(Adrian Singh).tap", 59653, 0xe3983c0e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMrdo, SpecMrdo, Spec128)
STD_ROM_FN(SpecMrdo)

struct BurnDriver BurnSpecMrdo = {
	"spec_mrdo", NULL, "spec_spec128", NULL, "2019",
	"Mr. Do! (HB)\0", NULL, "Adrian Singh", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMrdoRomInfo, SpecMrdoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Perils of Willy

static struct BurnRomInfo SpecPerilsofwillyRomDesc[] = {
	{ "Perils of Willy (2020)(highriser).tap", 26968, 0x19e22f49, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPerilsofwilly, SpecPerilsofwilly, Spec128)
STD_ROM_FN(SpecPerilsofwilly)

struct BurnDriver BurnSpecPerilsofwilly = {
	"spec_perilsofwilly", NULL, "spec_spec128", NULL, "2020",
	"Perils of Willy (HB)\0", NULL, "highriser", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPerilsofwillyRomInfo, SpecPerilsofwillyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Puta Mili

static struct BurnRomInfo SpecPutamiliRomDesc[] = {
	{ "Puta Mili (2020)(ejvg).tap", 39535, 0x04778248, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPutamili, SpecPutamili, Spec128)
STD_ROM_FN(SpecPutamili)

struct BurnDriver BurnSpecPutamili = {
	"spec_putamili", NULL, "spec_spec128", NULL, "2020",
	"Puta Mili (HB)\0", NULL, "ejvg", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPutamiliRomInfo, SpecPutamiliRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rat-A-Tat

static struct BurnRomInfo SpecRatatatRomDesc[] = {
	{ "Rat-A-Tat (2020)(JoeSoft).tap", 43232, 0x2d675b93, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRatatat, SpecRatatat, Spec128)
STD_ROM_FN(SpecRatatat)

struct BurnDriver BurnSpecRatatat = {
	"spec_ratatat", NULL, "spec_spec128", NULL, "2020",
	"Rat-A-Tat (HB)\0", NULL, "JoeSoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRatatatRomInfo, SpecRatatatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Vallation 128K (HB)

static struct BurnRomInfo SpecVallationRomDesc[] = {
	{ "Vallation (2017)(Psytronik).tap", 68513, 0xa30a4662, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecVallation, SpecVallation, Spec128)
STD_ROM_FN(SpecVallation)

struct BurnDriver BurnSpecVallation = {
	"spec_vallation", NULL, "spec_spec128", NULL, "2017",
	"Vallation 128K (HB)\0", NULL, "Psytronik", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecVallationRomInfo, SpecVallationRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Aquanoids (HB)

static struct BurnRomInfo SpecAquanoidsRomDesc[] = {
	{ "Aquanoids (2015)(Neil Parsons).tap", 38587, 0x6ead57d1, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAquanoids, SpecAquanoids, Spec128)
STD_ROM_FN(SpecAquanoids)

struct BurnDriver BurnSpecAquanoids = {
	"spec_aquanoids", NULL, "spec_spec128", NULL, "2015",
	"Aquanoids (HB)\0", NULL, "Neil Parsons", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAquanoidsRomInfo, SpecAquanoidsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Astrosmash! ZX

static struct BurnRomInfo SpecAstrosmashRomDesc[] = {
	{ "Astrosmash! ZX (2018)(AMCgames).tap", 23087, 0xb3fc0c89, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAstrosmash, SpecAstrosmash, Spec128)
STD_ROM_FN(SpecAstrosmash)

struct BurnDriver BurnSpecAstrosmash = {
	"spec_astrosmash", NULL, "spec_spec128", NULL, "2018",
	"Astrosmash! ZX (HB)\0", "Converted from Intellivision version", "AMCgames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_VERSHOOT, 0,
	SpectrumGetZipName, SpecAstrosmashRomInfo, SpecAstrosmashRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Baby Monkey Alba (HB)

static struct BurnRomInfo SpecBabymonkeyalbaRomDesc[] = {
	{ "Baby Monkey Alba (2017)(Javier Quero).tap", 43837, 0x50100dba, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBabymonkeyalba, SpecBabymonkeyalba, Spec128)
STD_ROM_FN(SpecBabymonkeyalba)

struct BurnDriver BurnSpecBabymonkeyalba = {
	"spec_babymonkeyalba", NULL, "spec_spec128", NULL, "2017",
	"Baby Monkey Alba (HB)\0", NULL, "Javier Quero", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecBabymonkeyalbaRomInfo, SpecBabymonkeyalbaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Biscuits In Hell (HB)

static struct BurnRomInfo SpecBiscuitsihRomDesc[] = {
	{ "Biscuits In Hell (2017)(Monument Microgames).tap", 36588, 0x7fefbfc7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBiscuitsih, SpecBiscuitsih, Spec128)
STD_ROM_FN(SpecBiscuitsih)

struct BurnDriver BurnSpecBiscuitsih = {
	"spec_biscuitsih", NULL, "spec_spec128", NULL, "2017",
	"Biscuits In Hell (HB)\0", NULL, "Monument Microgames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecBiscuitsihRomInfo, SpecBiscuitsihRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dingo (HB)

static struct BurnRomInfo SpecDingoRomDesc[] = {
	{ "Dingo (2015)(Sokurah, Tardis).tap", 41567, 0xb22b60c3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDingo, SpecDingo, Spec128)
STD_ROM_FN(SpecDingo)

struct BurnDriver BurnSpecDingo = {
	"spec_dingo", NULL, "spec_spec128", NULL, "2015",
	"Dingo (HB)\0", NULL, "Sokurah, Tardis", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecDingoRomInfo, SpecDingoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// FISTro Fighters (HB)

static struct BurnRomInfo SpecFistroRomDesc[] = {
	{ "FISTro Fighters (2015)(Alexhino).tap", 48506, 0x1a08f515, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFistro, SpecFistro, Spec128)
STD_ROM_FN(SpecFistro)

struct BurnDriver BurnSpecFistro = {
	"spec_fistro", NULL, "spec_spec128", NULL, "2016",
	"FISTro Fighters (HB)\0", NULL, "Retrobytes", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_VSFIGHT | GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecFistroRomInfo, SpecFistroRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Pack Bob (HB)

static struct BurnRomInfo SpecJetpackbobRomDesc[] = {
	{ "Jet Pack Bob (2017)(Douglas Bagnall).tap", 28945, 0xb2444927, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetpackbob, SpecJetpackbob, Spec128)
STD_ROM_FN(SpecJetpackbob)

struct BurnDriver BurnSpecJetpackbob = {
	"spec_jetpackbob", NULL, "spec_spec128", NULL, "2017",
	"Jet Pack Bob (HB)\0", "A clone of classic Jetpac", "Douglas Bagnall", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecJetpackbobRomInfo, SpecJetpackbobRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Monkey J - The Treasure Of The Gold Temple

static struct BurnRomInfo SpecMonkeyjRomDesc[] = {
	{ "Monkey J - The Treasure Of The Gold Temple (2017)(Gabriele Amore).tap", 35456, 0xc326ebce, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMonkeyj, SpecMonkeyj, Spec128)
STD_ROM_FN(SpecMonkeyj)

struct BurnDriver BurnSpecMonkeyj = {
	"spec_monkeyj", NULL, "spec_spec128", NULL, "2017",
	"Monkey J - The Treasure Of The Gold Temple (HB)\0", NULL, "Gabriele Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMonkeyjRomInfo, SpecMonkeyjRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moon Ranger

static struct BurnRomInfo SpecMoonrangerRomDesc[] = {
	{ "Moon Ranger (2020)(Gabriel Amore).tap", 29940, 0xb2177a3c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMoonranger, SpecMoonranger, Spec128)
STD_ROM_FN(SpecMoonranger)

struct BurnDriver BurnSpecMoonranger = {
	"spec_moonranger", NULL, "spec_spec128", NULL, "2020",
	"Moon Ranger (HB)\0", NULL, "Gabriel Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecMoonrangerRomInfo, SpecMoonrangerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Omelettes For Breakfast

static struct BurnRomInfo SpecOmelettesRomDesc[] = {
	{ "Omelettes For Breakfast (2017)(Gabriele Amore).tap", 35499, 0xd1706d0b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOmelettes, SpecOmelettes, Spec128)
STD_ROM_FN(SpecOmelettes)

struct BurnDriver BurnSpecOmelettes = {
	"spec_omelettes", NULL, "spec_spec128", NULL, "2017",
	"Omelettes For Breakfast (HB)\0", NULL, "Gabriele Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecOmelettesRomInfo, SpecOmelettesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Phaeton

static struct BurnRomInfo SpecPhaetonRomDesc[] = {
	{ "Phaeton (2010)(Rafat Niazga).tap", 34036, 0x2e9f7f1f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPhaeton, SpecPhaeton, Spec128)
STD_ROM_FN(SpecPhaeton)

struct BurnDriver BurnSpecPhaeton = {
	"spec_phaeton", NULL, "spec_spec128", NULL, "2010",
	"Phaeton (HB)\0", NULL, "Rafat Niazga", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecPhaetonRomInfo, SpecPhaetonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rabbit in Nightmareland

static struct BurnRomInfo SpecRabbitinnRomDesc[] = {
	{ "Rabbit in Nightmareland (2015)(Javier Fopiani).tap", 69594, 0xa0123467, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRabbitinn, SpecRabbitinn, Spec128)
STD_ROM_FN(SpecRabbitinn)

struct BurnDriver BurnSpecRabbitinn = {
	"spec_rabbitinn", NULL, "spec_spec128", NULL, "2015",
	"Rabbit in Nightmareland (HB)\0", NULL, "Javier Fopiani", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRabbitinnRomInfo, SpecRabbitinnRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Retroforce

static struct BurnRomInfo SpecRetroforceRomDesc[] = {
	{ "Retroforce (2017)(Climacus & KgMcNeil).tap", 64323, 0xc1c7cf3e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRetroforce, SpecRetroforce, Spec128)
STD_ROM_FN(SpecRetroforce)

struct BurnDriver BurnSpecRetroforce = {
	"spec_retroforce", NULL, "spec_spec128", NULL, "2017",
	"Retroforce (HB)\0", NULL, "Climacus & KgMcNeil", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecRetroforceRomInfo, SpecRetroforceRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Saving Kong

static struct BurnRomInfo SpecSavingkongRomDesc[] = {
	{ "Saving Kong (2018)(Gabriel Amore).tap", 40684, 0x84860fed, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSavingkong, SpecSavingkong, Spec128)
STD_ROM_FN(SpecSavingkong)

struct BurnDriver BurnSpecSavingkong = {
	"spec_savingkong", NULL, "spec_spec128", NULL, "2018",
	"Saving Kong (HB)\0", NULL, "Gabriel Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSavingkongRomInfo, SpecSavingkongRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Springbot - Mars Attack!

static struct BurnRomInfo SpecSpringbotRomDesc[] = {
	{ "Springbot - Mars Attack! (2020)(Andy Farrell).tap", 44477, 0x5345e0ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSpringbot, SpecSpringbot, Spec128)
STD_ROM_FN(SpecSpringbot)

struct BurnDriver BurnSpecSpringbot = {
	"spec_springbot", NULL, "spec_spec128", NULL, "2020",
	"Springbot - Mars Attack! (HB)\0", NULL, "Andy Farrell", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSpringbotRomInfo, SpecSpringbotRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// UFO

static struct BurnRomInfo SpecUfoRomDesc[] = {
	{ "UFO (2020)(F.J. Urbaneja).tap", 33701, 0x44cb0dbb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecUfo, SpecUfo, Spec128)
STD_ROM_FN(SpecUfo)

struct BurnDriver BurnSpecUfo = {
	"spec_ufo", NULL, "spec_spec128", NULL, "2020",
	"UFO (HB)\0", NULL, "F.J. Urbaneja", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecUfoRomInfo, SpecUfoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Los Amores De Brunilda

static struct BurnRomInfo SpecBrunildaRomDesc[] = {
	{ "Los Amores De Brunilda (2013)(RetroWorks).tap", 85072, 0xbfa8a647, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBrunilda, SpecBrunilda, Spec128)
STD_ROM_FN(SpecBrunilda)

struct BurnDriver BurnSpecBrunilda = {
	"spec_brunilda", NULL, "spec_spec128", NULL, "2013",
	"Los Amores De Brunilda (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecBrunildaRomInfo, SpecBrunildaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Captain Drexx

static struct BurnRomInfo SpecCaptaindrexxRomDesc[] = {
	{ "Captain Drexx (2014)(Vladimir Burenko).tap", 38766, 0x1c7be669, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCaptaindrexx, SpecCaptaindrexx, Spec128)
STD_ROM_FN(SpecCaptaindrexx)

struct BurnDriver BurnSpecCaptaindrexx = {
	"spec_captaindrexx", NULL, "spec_spec128", NULL, "2014",
	"Captain Drexx (HB)\0", NULL, "Vladimir Burenko", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecCaptaindrexxRomInfo, SpecCaptaindrexxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Complica DX

static struct BurnRomInfo SpecComplicadxRomDesc[] = {
	{ "Complica DX (2015)(Einar Saukas).tap", 48389, 0x4a72648f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecComplicadx, SpecComplicadx, Spec128)
STD_ROM_FN(SpecComplicadx)

struct BurnDriver BurnSpecComplicadx = {
	"spec_complicadx", NULL, "spec_spec128", NULL, "2015",
	"Complica DX (HB)\0", "Play with Keys 1-4", "Einar Saukas", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecComplicadxRomInfo, SpecComplicadxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Snake Escape

static struct BurnRomInfo SpecSnakeescapeRomDesc[] = {
	{ "Snake Escape (2016)(Einar Saukas).tap", 39093, 0x870cc8cd, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSnakeescape, SpecSnakeescape, Spec128)
STD_ROM_FN(SpecSnakeescape)

struct BurnDriver BurnSpecSnakeescape = {
	"spec_snakeescape", NULL, "spec_spec128", NULL, "2016",
	"Snake Escape (HB)\0", NULL, "Einar Saukas", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSnakeescapeRomInfo, SpecSnakeescapeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// splATTR

static struct BurnRomInfo SpecSplattrRomDesc[] = {
	{ "splATTR (2008)(Bob Smith).tap", 87068, 0xc1c7b410, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSplattr, SpecSplattr, Spec128)
STD_ROM_FN(SpecSplattr)

struct BurnDriver BurnSpecSplattr = {
	"spec_splattr", NULL, "spec_spec128", NULL, "2008",
	"splATTR (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSplattrRomInfo, SpecSplattrRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sun Bucket

static struct BurnRomInfo SpecSunbucketRomDesc[] = {
	{ "Sun Bucket (2014)(Stonechat).tap", 44159, 0xd9761fa3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSunbucket, SpecSunbucket, Spec128)
STD_ROM_FN(SpecSunbucket)

struct BurnDriver BurnSpecSunbucket = {
	"spec_sunbucket", NULL, "spec_spec128", NULL, "2014",
	"Sun Bucket (HB)\0", NULL, "Stonechat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSunbucketRomInfo, SpecSunbucketRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Dark

static struct BurnRomInfo SpecThedarkRomDesc[] = {
	{ "The Dark (1997)(Oleg Origin).tap", 42940, 0x8b93b94c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThedark, SpecThedark, Spec128)
STD_ROM_FN(SpecThedark)

struct BurnDriver BurnSpecThedark = {
	"spec_thedark", NULL, "spec_spec128", NULL, "1997",
	"The Dark (HB)\0", NULL, "Oleg Origin", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecThedarkRomInfo, SpecThedarkRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cattivik (HB)

static struct BurnRomInfo SpecCattivikRomDesc[] = {
	{ "Cattivik (2013)(Gabriele Amore, Alessandro Grussu).tap", 65828, 0x92e705bb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCattivik, SpecCattivik, Spec128)
STD_ROM_FN(SpecCattivik)

struct BurnDriver BurnSpecCattivik = {
	"spec_cattivik", NULL, "spec_spec128", NULL, "2013",
	"Cattivik (HB)\0", NULL, "Gabriele Amore, Alessandro Grussu", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecCattivikRomInfo, SpecCattivikRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// HELL YEAH! (HB)

static struct BurnRomInfo SpecHellyeahRomDesc[] = {
	{ "HELL YEAH! (2020)(Andy Precious).tap", 47848, 0x064685ab, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecHellyeah, SpecHellyeah, Spec128)
STD_ROM_FN(SpecHellyeah)

struct BurnDriver BurnSpecHellyeah = {
	"spec_hellyeah", NULL, "spec_spec128", NULL, "2020",
	"HELL YEAH! (HB)\0", NULL, "Andy Precious", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecHellyeahRomInfo, SpecHellyeahRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// AtomiCat (HB)

static struct BurnRomInfo SpecAtomicatRomDesc[] = {
	{ "AtomiCat (2020)(Poe Games).tap", 40363, 0xe4f56a9d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAtomicat, SpecAtomicat, Spec128)
STD_ROM_FN(SpecAtomicat)

struct BurnDriver BurnSpecAtomicat = {
	"spec_atomicat", NULL, "spec_spec128", NULL, "2020",
	"AtomiCat (HB)\0", NULL, "Poe Games", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAtomicatRomInfo, SpecAtomicatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bat Boy (HB)

static struct BurnRomInfo SpecBatboyRomDesc[] = {
	{ "Bat Boy (2020)(Antonio Perez).tap", 55621, 0x354abdb7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBatboy, SpecBatboy, Spec128)
STD_ROM_FN(SpecBatboy)

struct BurnDriver BurnSpecBatboy = {
	"spec_batboy", NULL, "spec_spec128", NULL, "2020",
	"Bat Boy (HB)\0", NULL, "Antonio Perez", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecBatboyRomInfo, SpecBatboyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bonnie and Clyde (HB)

static struct BurnRomInfo SpecBonnieclydeRomDesc[] = {
	{ "Bonnie and Clyde (1986)(Zosya).tap", 47115, 0xf075ba62, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBonnieclyde, SpecBonnieclyde, Spec128)
STD_ROM_FN(SpecBonnieclyde)

struct BurnDriver BurnSpecBonnieclyde = {
	"spec_bonnieclyde", NULL, "spec_spec128", NULL, "2020",
	"Bonnie and Clyde (HB)\0", NULL, "Zosya", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecBonnieclydeRomInfo, SpecBonnieclydeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Patrick Paddle (HB)

static struct BurnRomInfo SpecPatrickpaddleRomDesc[] = {
	{ "Patrick Paddle (2020)(PROSM Software).tap", 13883, 0x952f6855, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPatrickpaddle, SpecPatrickpaddle, Spec128)
STD_ROM_FN(SpecPatrickpaddle)

struct BurnDriver BurnSpecPatrickpaddle = {
	"spec_patrickpaddle", NULL, "spec_spec128", NULL, "2020",
	"Patrick Paddle (HB)\0", NULL, "PROSM Software", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecPatrickpaddleRomInfo, SpecPatrickpaddleRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Trinidad (HB)

static struct BurnRomInfo SpecTrinidadRomDesc[] = {
	{ "Trinidad (2020)(Polybius).tap", 43020, 0x4879e658, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTrinidad, SpecTrinidad, Spec128)
STD_ROM_FN(SpecTrinidad)

struct BurnDriver BurnSpecTrinidad = {
	"spec_trinidad", NULL, "spec_spec128", NULL, "2020",
	"Trinidad (HB)\0", NULL, "Polybius", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecTrinidadRomInfo, SpecTrinidadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Albert The Wolf (HB)

static struct BurnRomInfo SpecAlbertwolfRomDesc[] = {
	{ "Albert The Wolf (2014)(Gabriele Amore).tap", 33984, 0xae388913, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAlbertwolf, SpecAlbertwolf, Spec128)
STD_ROM_FN(SpecAlbertwolf)

struct BurnDriver BurnSpecAlbertwolf = {
	"spec_albertwolf", NULL, "spec_spec128", NULL, "2014",
	"Albert The Wolf (HB)\0", NULL, "Gabriele Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAlbertwolfRomInfo, SpecAlbertwolfRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bandito (HB)

static struct BurnRomInfo SpecBanditoRomDesc[] = {
	{ "Bandito (2020)(Andy McDermott).tap", 34018, 0x3591be21, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBandito, SpecBandito, Spec128)
STD_ROM_FN(SpecBandito)

struct BurnDriver BurnSpecBandito = {
	"spec_bandito", NULL, "spec_spec128", NULL, "2020",
	"Bandito (HB)\0", NULL, "Andy McDermott", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecBanditoRomInfo, SpecBanditoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Blizzard's Rift (HB)

static struct BurnRomInfo SpecBlizzardsriftRomDesc[] = {
	{ "Blizzard's Rift (2007)(Jonathan Cauldwell).tap", 66068, 0x884eef54, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBlizzardsrift, SpecBlizzardsrift, Spec128)
STD_ROM_FN(SpecBlizzardsrift)

struct BurnDriver BurnSpecBlizzardsrift = {
	"spec_blizzardsrift", NULL, "spec_spec128", NULL, "2007",
	"Blizzard's Rift (HB)\0", NULL, "Jonathan Cauldwell", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecBlizzardsriftRomInfo, SpecBlizzardsriftRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Buzzsaw+ (HB)

static struct BurnRomInfo SpecBuzzsawRomDesc[] = {
	{ "Buzzsaw+ (2011)(Jason J. Railton).tap", 42988, 0xf066f06d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBuzzsaw, SpecBuzzsaw, Spec128)
STD_ROM_FN(SpecBuzzsaw)

struct BurnDriver BurnSpecBuzzsaw = {
	"spec_buzzsaw", NULL, "spec_spec128", NULL, "2011",
	"Buzzsaw+ (HB)\0", NULL, "Jason J. Railton", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecBuzzsawRomInfo, SpecBuzzsawRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// C is for Crabby (HB)

static struct BurnRomInfo SpecCcrabbyRomDesc[] = {
	{ "C is for Crabby (2013)(Gabriel Amore).tap", 41821, 0x490a07d9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCcrabby, SpecCcrabby, Spec128)
STD_ROM_FN(SpecCcrabby)

struct BurnDriver BurnSpecCcrabby = {
	"spec_ccrabby", NULL, "spec_spec128", NULL, "2013",
	"C is for Crabby (HB)\0", NULL, "Gabriel Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecCcrabbyRomInfo, SpecCcrabbyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Castlevania - Spectral Interlude (128K)(HB)

static struct BurnRomInfo SpeccastlevaniaRomDesc[] = {
	{ "Castlevania - Spectral Interlude (2015)(Rewind Team).tap", 0x1edba, 0xc100bb38, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccastlevania, Speccastlevania, Spec128)
STD_ROM_FN(Speccastlevania)

struct BurnDriver BurnSpeccastlevania = {
	"spec_castlevania", NULL, "spec_spec128", NULL, "2015",
	"Castlevania - Spectral Interlude (128K)(HB)\0", NULL, "Rewind Team", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpeccastlevaniaRomInfo, SpeccastlevaniaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Castlevania - Spectral Interlude (128K)(HB, Russian)

static struct BurnRomInfo SpeccastlevanrusRomDesc[] = {
	{ "Castlevania - Spectral Interlude (2015)(Rewind Team)(Rus).tap", 0x1f04e, 0x45561b70, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Speccastlevanrus, Speccastlevanrus, Spec128)
STD_ROM_FN(Speccastlevanrus)

struct BurnDriver BurnSpeccastlevanrus = {
	"spec_castlevanrus", "spec_castlevania", "spec_spec128", NULL, "2015",
	"Castlevania - Spectral Interlude (128k)(HB, Russian)\0", NULL, "Rewind Team", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpeccastlevanrusRomInfo, SpeccastlevanrusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chip Rescue (HB)

static struct BurnRomInfo SpecChiprescueRomDesc[] = {
	{ "Chip Rescue (2019)(romancha).tap", 19111, 0x917b33c6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecChiprescue, SpecChiprescue, Spec128)
STD_ROM_FN(SpecChiprescue)

struct BurnDriver BurnSpecChiprescue = {
	"spec_chiprescue", NULL, "spec_spec128", NULL, "2019",
	"Chip Rescue (HB)\0", "Press 0 to start", "romancha", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecChiprescueRomInfo, SpecChiprescueRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecIntf2DIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Chopper Drop (HB, v1.1)

static struct BurnRomInfo SpecChopperdropRomDesc[] = {
	{ "Chopper Drop (2011)(Paul Jenkinson).tap", 31591, 0x8c4eb3fe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecChopperdrop, SpecChopperdrop, Spec128)
STD_ROM_FN(SpecChopperdrop)

struct BurnDriver BurnSpecChopperdrop = {
	"spec_chopperdrop", NULL, "spec_spec128", NULL, "2011",
	"Chopper Drop (HB, v1.1)\0", NULL, "Paul Jenkinson", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecChopperdropRomInfo, SpecChopperdropRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Droid Buster (HB)

static struct BurnRomInfo SpecDroidbusterRomDesc[] = {
	{ "Droid Buster (2019)(Ariel Endaraues).tap", 45669, 0x4c635bbe, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDroidbuster, SpecDroidbuster, Spec128)
STD_ROM_FN(SpecDroidbuster)

struct BurnDriver BurnSpecDroidbuster = {
	"spec_droidbuster", NULL, "spec_spec128", NULL, "2019",
	"Droid Buster (HB)\0", NULL, "Ariel Endaraues", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_MAZE, 0,
	SpectrumGetZipName, SpecDroidbusterRomInfo, SpecDroidbusterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fireman (HB)

static struct BurnRomInfo SpecFiremanRomDesc[] = {
	{ "Fireman (2008)(Miguel Angel Rodriguez Jodar).tap", 18452, 0xf7853e31, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFireman, SpecFireman, Spec128)
STD_ROM_FN(SpecFireman)

struct BurnDriver BurnSpecFireman = {
	"spec_fireman", NULL, "spec_spec128", NULL, "2008",
	"Fireman (HB)\0", NULL, "Miguel Angel Rodriguez Jodar", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecFiremanRomInfo, SpecFiremanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// The Lost Tapes of Albion (HB)

static struct BurnRomInfo SpecLosttapesRomDesc[] = {
	{ "The Lost Tapes of Albion (2012)(Stonechat Productions).tap", 25472, 0x0f3caf6e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLosttapes, SpecLosttapes, Spec128)
STD_ROM_FN(SpecLosttapes)

struct BurnDriver BurnSpecLosttapes = {
	"spec_losttapes", NULL, "spec_spec128", NULL, "2012",
	"The Lost Tapes of Albion (HB)\0", NULL, "Stonechat Productions", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecLosttapesRomInfo, SpecLosttapesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pets vs. Aliens (HB)

static struct BurnRomInfo SpecPetsvsaliensRomDesc[] = {
	{ "Pets vs. Aliens (2015)(Einar Saukas).tap", 42877, 0x3bb9a912, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPetsvsaliens, SpecPetsvsaliens, Spec128)
STD_ROM_FN(SpecPetsvsaliens)

struct BurnDriver BurnSpecPetsvsaliens = {
	"spec_petsvsaliens", NULL, "spec_spec128", NULL, "2015",
	"Pets vs. Aliens (HB)\0", NULL, "Einar Saukas", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 2, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecPetsvsaliensRomInfo, SpecPetsvsaliensRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Space Monsters meet The Hardy (HB)

static struct BurnRomInfo SpecThehardyRomDesc[] = {
	{ "Space Monsters meet The Hardy (2019)(Mayhem).tap", 87134, 0x96c02acc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecThehardy, SpecThehardy, Spec128)
STD_ROM_FN(SpecThehardy)

struct BurnDriver BurnSpecThehardy = {
	"spec_thehardy", NULL, "spec_spec128", NULL, "2019",
	"Space Monsters meet The Hardy (HB)\0", NULL, "Mayhem", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecThehardyRomInfo, SpecThehardyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// LumASCII (HB)

static struct BurnRomInfo SpecLumasciiRomDesc[] = {
	{ "LumASCII (2012)(Bob's Stuff).tap", 47917, 0xf29c2da7, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLumascii, SpecLumascii, Spec128)
STD_ROM_FN(SpecLumascii)

struct BurnDriver BurnSpecLumascii = {
	"spec_lumascii", NULL, "spec_spec128", NULL, "2012",
	"LumASCII (HB)\0", NULL, "Bob's Stuff", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecLumasciiRomInfo, SpecLumasciiRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mike the Guitar - The Shooter (HB)

static struct BurnRomInfo SpecMiketheguitarRomDesc[] = {
	{ "Mike the Guitar - The Shooter (2019)(Sebastian Braunert).tap", 42463, 0x02644948, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMiketheguitar, SpecMiketheguitar, Spec128)
STD_ROM_FN(SpecMiketheguitar)

struct BurnDriver BurnSpecMiketheguitar = {
	"spec_miketheguitar", NULL, "spec_spec128", NULL, "2019",
	"Mike the Guitar - The Shooter (HB)\0", NULL, "Sebastian Braunert", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecMiketheguitarRomInfo, SpecMiketheguitarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Moderate Retribution (HB)

static struct BurnRomInfo SpecModerateretributionRomDesc[] = {
	{ "Moderate Retribution (2013)(Stonechat).tap", 30951, 0x29efa9e5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecModerateretribution, SpecModerateretribution, Spec128)
STD_ROM_FN(SpecModerateretribution)

struct BurnDriver BurnSpecModerateretribution = {
	"spec_moderateretribution", NULL, "spec_spec128", NULL, "2013",
	"Moderate Retribution (HB)\0", NULL, "Stonechat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecModerateretributionRomInfo, SpecModerateretributionRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Percy Penguin (HB)

static struct BurnRomInfo SpecPercypenguinRomDesc[] = {
	{ "Percy Penguin (2019)(John Davies).tap", 47843, 0xd944ba7a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPercypenguin, SpecPercypenguin, Spec128)
STD_ROM_FN(SpecPercypenguin)

struct BurnDriver BurnSpecPercypenguin = {
	"spec_percypenguin", NULL, "spec_spec128", NULL, "2019",
	"Percy Penguin (HB)\0", NULL, "John Davies", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecPercypenguinRomInfo, SpecPercypenguinRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Trooper Point 5

static struct BurnRomInfo SpecTrooperpoint5RomDesc[] = {
	{ "Trooper Point 5 (2010)(The Bog Brothers).tap", 38475, 0xd17b61d0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTrooperpoint5, SpecTrooperpoint5, Spec128)
STD_ROM_FN(SpecTrooperpoint5)

struct BurnDriver BurnSpecTrooperpoint5 = {
	"spec_trooperpoint5", NULL, "spec_spec128", NULL, "2010",
	"Trooper Point 5 (HB)\0", NULL, "The Bog Brothers", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecTrooperpoint5RomInfo, SpecTrooperpoint5RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yars Revenge ZX

static struct BurnRomInfo SpecYarsrevengeRomDesc[] = {
	{ "Yars Revenge ZX (2015)(Monument Microgames).tap", 31227, 0x9d59babf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYarsrevenge, SpecYarsrevenge, Spec128)
STD_ROM_FN(SpecYarsrevenge)

struct BurnDriver BurnSpecYarsrevenge = {
	"spec_yarsrevenge", NULL, "spec_spec128", NULL, "2015",
	"Yars Revenge ZX (HB)\0", NULL, "Monument Microgames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecYarsrevengeRomInfo, SpecYarsrevengeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Alien Girl - 48k Demo

static struct BurnRomInfo SpecAliengirldemoRomDesc[] = {
	{ "Alien Girl - 48k Demo (2020)(Javier Fopiani).tap", 44475, 0x4e1c2f21, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAliengirldemo, SpecAliengirldemo, Spectrum)
STD_ROM_FN(SpecAliengirldemo)

struct BurnDriver BurnSpecAliengirldemo = {
	"spec_aliengirldemo", NULL, "spec_spectrum", NULL, "2020",
	"Alien Girl - 48k Demo (HB)\0", NULL, "Javier Fopiani", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecAliengirldemoRomInfo, SpecAliengirldemoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Generacio Digital (HB)

static struct BurnRomInfo SpecGenerdigitalRomDesc[] = {
	{ "Generacio Digital (2019)(Catalunya Radio).tap", 39117, 0x76ec9f7d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGenerdigital, SpecGenerdigital, Spec128)
STD_ROM_FN(SpecGenerdigital)

struct BurnDriver BurnSpecGenerdigital = {
	"spec_generdigital", NULL, "spec_spec128", NULL, "2019",
	"Generacio Digital (HB)\0", "Select keyboard to play - game inputs set on QAOP-SPACE", "Catalunya Radio", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecGenerdigitalRomInfo, SpecGenerdigitalRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Space Journey (HB)

static struct BurnRomInfo SpecSpacejourneyRomDesc[] = {
	{ "Space Journey (2020)(Roman Cikryt).tap", 46896, 0xb55f0534, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSpacejourney, SpecSpacejourney, Spec128)
STD_ROM_FN(SpecSpacejourney)

struct BurnDriver BurnSpecSpacejourney = {
	"spec_spacejourney", NULL, "spec_spec128", NULL, "2020",
	"Space Journey (HB)\0", NULL, "Roman Cikryt", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecSpacejourneyRomInfo, SpecSpacejourneyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPMDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Duckstroma v1.02 (HB)

static struct BurnRomInfo SpecduckstromaRomDesc[] = {
	{ "Duckstroma v1.02 (2020)(UltraNarwhal).z80", 41142, 0xf75a1350, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specduckstroma, Specduckstroma, Spec128)
STD_ROM_FN(Specduckstroma)

struct BurnDriver BurnSpecduckstroma = {
	"spec_duckstroma", NULL, "spec_spec128", NULL, "2020",
	"Duckstroma (HB, v1.02)\0", NULL, "UltraNarwhal", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecduckstromaRomInfo, SpecduckstromaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Farmer Jack in Harvest Havoc (HB)

static struct BurnRomInfo SpecFjhhRomDesc[] = {
	{ "Farmer Jack in Harvest Havoc (2006)(Bob Smith).tap", 39223, 0x936f2345, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFjhh, SpecFjhh, Spec128)
STD_ROM_FN(SpecFjhh)

struct BurnDriver BurnSpecFjhh = {
	"spec_fjhh", NULL, "spec_spec128", NULL, "2006",
	"Farmer Jack in Harvest Havoc (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecFjhhRomInfo, SpecFjhhRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Farmer Jack and the Hedge Monkeys (HB)

static struct BurnRomInfo SpecFjhmRomDesc[] = {
	{ "Farmer Jack and the Hedge Monkeys (2008)(Bob Smith).tap", 48405, 0xde828b58, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFjhm, SpecFjhm, Spec128)
STD_ROM_FN(SpecFjhm)

struct BurnDriver BurnSpecFjhm = {
	"spec_fjhm", NULL, "spec_spec128", NULL, "2008",
	"Farmer Jack and the Hedge Monkeys (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecFjhmRomInfo, SpecFjhmRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jilly's Farm Volume 1 - SokoBAArn (HB)

static struct BurnRomInfo SpecJillysfarm1RomDesc[] = {
	{ "Jilly's Farm Volume 1 - SokoBAArn (2018)(Bob Smith).tap", 47687, 0x52c86488, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJillysfarm1, SpecJillysfarm1, Spec128)
STD_ROM_FN(SpecJillysfarm1)

struct BurnDriver BurnSpecJillysfarm1 = {
	"spec_jillysfarm1", NULL, "spec_spec128", NULL, "2018",
	"Jilly's Farm Volume 1 - SokoBAArn (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecJillysfarm1RomInfo, SpecJillysfarm1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Magenta Jim (HB)

static struct BurnRomInfo SpecMagentajimRomDesc[] = {
	{ "Magenta Jim (2020)(Gaz Marshall).tap", 23797, 0x1fee648d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMagentajim, SpecMagentajim, Spec128)
STD_ROM_FN(SpecMagentajim)

struct BurnDriver BurnSpecMagentajim = {
	"spec_magentajim", NULL, "spec_spec128", NULL, "2020",
	"Magenta Jim (HB)\0", NULL, "Gaz Marshall", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecMagentajimRomInfo, SpecMagentajimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Night Stalker ZX (HB)

static struct BurnRomInfo SpecNightstalkerRomDesc[] = {
	{ "Night Stalker ZX (2018)(AMCgames).tap", 34047, 0x6c112118, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNightstalker, SpecNightstalker, Spec128)
STD_ROM_FN(SpecNightstalker)

struct BurnDriver BurnSpecNightstalker = {
	"spec_nightstalker", NULL, "spec_spec128", NULL, "2018",
	"Night Stalker ZX (HB)\0", NULL, "AMCgames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecNightstalkerRomInfo, SpecNightstalkerRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninja Gardening-Simulator (HB)

static struct BurnRomInfo SpecNinjagardensimRomDesc[] = {
	{ "Ninja Gardening-Simulator (2019)(Andrew Bunker).tap", 45008, 0x80d96a46, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNinjagardensim, SpecNinjagardensim, Spec128)
STD_ROM_FN(SpecNinjagardensim)

struct BurnDriver BurnSpecNinjagardensim = {
	"spec_ninjagardensim", NULL, "spec_spec128", NULL, "2019",
	"Ninja Gardening-Simulator (HB)\0", NULL, "Andrew Bunker", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecNinjagardensimRomInfo, SpecNinjagardensimRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nosy (HB)

static struct BurnRomInfo SpecNosyRomDesc[] = {
	{ "Nosy (2020)(Javier Fopiani).tap", 43998, 0xefb0331a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNosy, SpecNosy, Spec128)
STD_ROM_FN(SpecNosy)

struct BurnDriver BurnSpecNosy = {
	"spec_nosy", NULL, "spec_spec128", NULL, "2020",
	"Nosy (HB)\0", NULL, "Javier Fopiani", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecNosyRomInfo, SpecNosyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Pumpkin Poe (HB)

static struct BurnRomInfo SpecPumpkinpoeRomDesc[] = {
	{ "Pumpkin Poe (2020)(Ariel Endaraues).tap", 39685, 0x64d9129d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecPumpkinpoe, SpecPumpkinpoe, Spec128)
STD_ROM_FN(SpecPumpkinpoe)

struct BurnDriver BurnSpecPumpkinpoe = {
	"spec_pumpkinpoe", NULL, "spec_spec128", NULL, "2020",
	"Pumpkin Poe (HB)\0", NULL, "Ariel Endaraues", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecPumpkinpoeRomInfo, SpecPumpkinpoeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Red Planet (HB)

static struct BurnRomInfo SpecRedplanetRomDesc[] = {
	{ "Red Planet (2015)(salvakantero).tap", 42978, 0x6b8baac0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRedplanet, SpecRedplanet, Spec128)
STD_ROM_FN(SpecRedplanet)

struct BurnDriver BurnSpecRedplanet = {
	"spec_redplanet", NULL, "spec_spec128", NULL, "2015",
	"Red Planet (HB)\0", NULL, "salvakantero", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRedplanetRomInfo, SpecRedplanetRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sc0tb0t (HB)

static struct BurnRomInfo SpecSc0tb0tRomDesc[] = {
	{ "Sc0tb0t (2020)(Dougie mcg).tap", 37704, 0x8848666f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSc0tb0t, SpecSc0tb0t, Spec128)
STD_ROM_FN(SpecSc0tb0t)

struct BurnDriver BurnSpecSc0tb0t = {
	"spec_sc0tb0t", NULL, "spec_spec128", NULL, "2020",
	"Sc0tb0t (HB)\0", NULL, "Dougie mcg", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecSc0tb0tRomInfo, SpecSc0tb0tRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Stranded 2.5 (HB)

static struct BurnRomInfo SpecStranded25RomDesc[] = {
	{ "Stranded 2.5 (2007)(Bob Smith).tap", 38037, 0x7b9f1cec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecStranded25, SpecStranded25, Spec128)
STD_ROM_FN(SpecStranded25)

struct BurnDriver BurnSpecStranded25 = {
	"spec_stranded25", NULL, "spec_spec128", NULL, "2007",
	"Stranded 2.5 (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecStranded25RomInfo, SpecStranded25RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// WHB (HB)

static struct BurnRomInfo SpecWhbRomDesc[] = {
	{ "WHB (2009)(Bob Smith).tap", 47750, 0xd55e0561, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWhb, SpecWhb, Spec128)
STD_ROM_FN(SpecWhb)

struct BurnDriver BurnSpecWhb = {
	"spec_whb", NULL, "spec_spec128", NULL, "2009",
	"WHB (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecWhbRomInfo, SpecWhbRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Wudang (HB)

static struct BurnRomInfo SpecWudangRomDesc[] = {
	{ "Wudang (2020)(World XXI Soft).tap", 22457, 0x68b849e6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWudang, SpecWudang, Spec128)
STD_ROM_FN(SpecWudang)

struct BurnDriver BurnSpecWudang = {
	"spec_wudang", NULL, "spec_spec128", NULL, "2020",
	"Wudang (HB)\0", NULL, "World XXI Soft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecWudangRomInfo, SpecWudangRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// X=Y=Z (HB)

static struct BurnRomInfo SpecXyzRomDesc[] = {
	{ "X=Y=Z (2014)(Bob Smith).tap", 18555, 0x44da8f4e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecXyz, SpecXyz, Spec128)
STD_ROM_FN(SpecXyz)

struct BurnDriver BurnSpecXyz = {
	"spec_xyz", NULL, "spec_spec128", NULL, "2014",
	"X=Y=Z (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecXyzRomInfo, SpecXyzRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Bullet Storm (HB)

static struct BurnRomInfo SpecBulletstormRomDesc[] = {
	{ "Bullet Storm (2020)(VolatiL).tap", 48736, 0x16a8ccf9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecBulletstorm, SpecBulletstorm, Spec128)
STD_ROM_FN(SpecBulletstorm)

struct BurnDriver BurnSpecBulletstorm = {
	"spec_bulletstorm", NULL, "spec_spec128", NULL, "2020",
	"Bullet Storm (HB)\0", NULL, "VolatiL", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecBulletstormRomInfo, SpecBulletstormRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Captain Gofer (HB, Russian)

static struct BurnRomInfo SpecCaptaingoferRomDesc[] = {
	{ "Captain Gofer (2020)(Dwa83).z80", 27917, 0xf843ec65, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCaptaingofer, SpecCaptaingofer, Spec128)
STD_ROM_FN(SpecCaptaingofer)

struct BurnDriver BurnSpecCaptaingofer = {
	"spec_captaingofer", NULL, "spec_spec128", NULL, "2020",
	"Captain Gofer (HB, Russian)\0", NULL, "Dwa83", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN, 0,
	SpectrumGetZipName, SpecCaptaingoferRomInfo, SpecCaptaingoferRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Code-112 (HB)

static struct BurnRomInfo SpecCode112RomDesc[] = {
	{ "Code-112 (2020)(PCNONOGames).tap", 47864, 0xd3a6349d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCode112, SpecCode112, Spec128)
STD_ROM_FN(SpecCode112)

struct BurnDriver BurnSpecCode112 = {
	"spec_code112", NULL, "spec_spec128", NULL, "2020",
	"Code-112 (HB)\0", NULL, "PCNONOGames", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecCode112RomInfo, SpecCode112RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cosmic Payback (HB)

static struct BurnRomInfo SpecCosmicpaybackRomDesc[] = {
	{ "Cosmic Payback (2020)(John Connolly).tap", 58570, 0xc91aa72f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCosmicpayback, SpecCosmicpayback, Spec128)
STD_ROM_FN(SpecCosmicpayback)

struct BurnDriver BurnSpecCosmicpayback = {
	"spec_cosmicpayback", NULL, "spec_spec128", NULL, "2020",
	"Cosmic Payback (HB)\0", NULL, "John Connolly", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING| BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecCosmicpaybackRomInfo, SpecCosmicpaybackRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cygnus - Alpha (HB)

static struct BurnRomInfo SpecCygnusalphaRomDesc[] = {
	{ "Cygnus - Alpha (2020)(ILFORD).z80", 50933, 0x2096d6d4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCygnusalpha, SpecCygnusalpha, Spec128)
STD_ROM_FN(SpecCygnusalpha)

struct BurnDriver BurnSpecCygnusalpha = {
	"spec_cygnusalpha", NULL, "spec_spec128", NULL, "2020",
	"Cygnus - Alpha (HB)\0", NULL, "ILFORD", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_SHOOT, 0,
	SpectrumGetZipName, SpecCygnusalphaRomInfo, SpecCygnusalphaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Enigmatik (HB)

static struct BurnRomInfo SpecEnignimatikRomDesc[] = {
	{ "Enigmatik (2020)(IADVD).tap", 45841, 0xc02dcd01, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEnignimatik, SpecEnignimatik, Spec128)
STD_ROM_FN(SpecEnignimatik)

struct BurnDriver BurnSpecEnignimatik = {
	"spec_enignimatik", NULL, "spec_spec128", NULL, "2020",
	"Enigmatik (HB)\0", NULL, "IADVD", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecEnignimatikRomInfo, SpecEnignimatikRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Marsmare - Alienation (HB)

static struct BurnRomInfo SpecMarsmarealienationRomDesc[] = {
	{ "Marsmare - Alienation (2020)(Drunk Fly).tap", 115080, 0x906f7098, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMarsmarealienation, SpecMarsmarealienation, Spec128)
STD_ROM_FN(SpecMarsmarealienation)

struct BurnDriver BurnSpecMarsmarealienation = {
	"spec_marsmarealienation", NULL, "spec_spec128", NULL, "2020",
	"Marsmare - Alienation (HB)\0", NULL, "Drunk Fly", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING| BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMarsmarealienationRomInfo, SpecMarsmarealienationRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Red Raid - The Beginning (HB, Side A)

static struct BurnRomInfo SpecRedraidside1RomDesc[] = {
	{ "Red Raid - The Beginning, Side A (2020)(ZXBitles).tap", 46995, 0xd768649a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRedraidside1, SpecRedraidside1, Spec128)
STD_ROM_FN(SpecRedraidside1)

struct BurnDriver BurnSpecRedraidside1 = {
	"spec_redraidside1", NULL, "spec_spec128", NULL, "2020",
	"Red Raid - The Beginning (HB, Side A)\0", NULL, "ZXBitles", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRedraidside1RomInfo, SpecRedraidside1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Red Raid - The Beginning (HB, Side B)

static struct BurnRomInfo SpecRedraidside2RomDesc[] = {
	{ "Red Raid - The Beginning, Side B (2020)(ZXBitles).tap", 47521, 0xd534f9c0, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRedraidside2, SpecRedraidside2, Spec128)
STD_ROM_FN(SpecRedraidside2)

struct BurnDriver BurnSpecRedraidside2 = {
	"spec_redraidside2", "spec_redraidside1", "spec_spec128", NULL, "2020",
	"Red Raid - The Beginning (HB, Side B)\0", NULL, "ZXBitles", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRedraidside2RomInfo, SpecRedraidside2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sixsixsixsix (HB, Russian)

static struct BurnRomInfo SpecSixsixsixsixRomDesc[] = {
	{ "Sixsixsixsix (2020)(Anhot Studio).tap", 42560, 0xaaae62a6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecSixsixsixsix, SpecSixsixsixsix, Spec128)
STD_ROM_FN(SpecSixsixsixsix)

struct BurnDriver BurnSpecSixsixsixsix = {
	"spec_sixsixsixsix", NULL, "spec_spec128", NULL, "2020",
	"Sixsixsixsix (HB, Russian)\0", NULL, "Anhot Studio", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecSixsixsixsixRomInfo, SpecSixsixsixsixRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Techno Alice (HB, Russian)(48k)

static struct BurnRomInfo SpecTechnoaliceRomDesc[] = {
	{ "Techno Alice (2020)(ALKO).tap", 44226, 0x6e631df4, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTechnoalice, SpecTechnoalice, Spectrum)
STD_ROM_FN(SpecTechnoalice)

struct BurnDriver BurnSpecTechnoalice = {
	"spec_technoalice", NULL, "spec_spectrum", NULL, "2020",
	"Techno Alice (HB, Russian)(48k)\0", NULL, "ALKO", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING| BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecTechnoaliceRomInfo, SpecTechnoaliceRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// White Jaguar (HB)

static struct BurnRomInfo SpecWhitejaguarRomDesc[] = {
	{ "White Jaguar (2020)(Romancha).tap", 24324, 0xbb1edf8f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecWhitejaguar, SpecWhitejaguar, Spec128)
STD_ROM_FN(SpecWhitejaguar)

struct BurnDriver BurnSpecWhitejaguar = {
	"spec_whitejaguar", NULL, "spec_spec128", NULL, "2020",
	"White Jaguar (HB)\0", NULL, "Romancha", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecWhitejaguarRomInfo, SpecWhitejaguarRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yoyo's Great Adventure (HB)

static struct BurnRomInfo SpecYoyogreatadventureRomDesc[] = {
	{ "Yoyo's Great Adventure (2020)(Rafal Miazga).tap", 48287, 0xa4f5b9b9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYoyogreatadventure, SpecYoyogreatadventure, Spec128)
STD_ROM_FN(SpecYoyogreatadventure)

struct BurnDriver BurnSpecYoyogreatadventure = {
	"spec_yoyogreatadventure", NULL, "spec_spec128", NULL, "2020",
	"Yoyo's Great Adventure (HB)\0", NULL, "Rafal Miazga", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_RPG, 0,
	SpectrumGetZipName, SpecYoyogreatadventureRomInfo, SpecYoyogreatadventureRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy VIII - Wonderful Dizzy (HB)

static struct BurnRomInfo SpecDizzy8RomDesc[] = {
	{ "Dizzy VIII - Wonderful Dizzy (2020)(Team Yolkfolk).tap", 116323, 0x0f8b91f8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDizzy8, SpecDizzy8, Spec128)
STD_ROM_FN(SpecDizzy8)

struct BurnDriver BurnSpecDizzy8 = {
	"spec_dizzy8", NULL, "spec_spec128", NULL, "2020",
	"Dizzy VIII - Wonderful Dizzy (HB)\0", NULL, "Team Yolkfolk", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecDizzy8RomInfo, SpecDizzy8RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mousetrap (HB)

static struct BurnRomInfo SpecMousetrapRomDesc[] = {
	{ "Mousetrap (2020)(Chris Maling).tap", 10185, 0x170bd6b3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMousetrap, SpecMousetrap, Spec128)
STD_ROM_FN(SpecMousetrap)

struct BurnDriver BurnSpecMousetrap = {
	"spec_mousetrap", NULL, "spec_spec128", NULL, "2020",
	"Mousetrap (HB)\0", NULL, "Chris Maling", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecMousetrapRomInfo, SpecMousetrapRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Roger the Pangolin in 2020 Knurled Tour (HB)

static struct BurnRomInfo SpecRogerpangolinRomDesc[] = {
	{ "Roger the Pangolin in 2020 Knurled Tour (2020)(Joefish).tap", 42586, 0x94fea62c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRogerpangolin, SpecRogerpangolin, Spec128)
STD_ROM_FN(SpecRogerpangolin)

struct BurnDriver BurnSpecRogerpangolin = {
	"spec_rogerpangolin", NULL, "spec_spec128", NULL, "2020",
	"Roger the Pangolin in 2020 Knurled Tour (HB)\0", NULL, "Joefish", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecRogerpangolinRomInfo, SpecRogerpangolinRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// A Very Sheepy Christmas (HB)

static struct BurnRomInfo SpecVerysheepyxmasRomDesc[] = {
	{ "A Very Sheepy Christmas (2020)(Quantum Sheep).tap", 27498, 0x94185496, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecVerysheepyxmas, SpecVerysheepyxmas, Spec128)
STD_ROM_FN(SpecVerysheepyxmas)

struct BurnDriver BurnSpecVerysheepyxmas = {
	"spec_verysheepyxmas", NULL, "spec_spec128", NULL, "2020",
	"A Very Sheepy Christmas (HB)\0", NULL, "Quantum Sheep", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecVerysheepyxmasRomInfo, SpecVerysheepyxmasRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dark Redux, The (HB)

static struct BurnRomInfo SpecDarkreduxRomDesc[] = {
	{ "Dark Redux, The (2021)(Zosya Entertainment).tap", 46873, 0x34f3deef, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDarkredux, SpecDarkredux, Spec128)
STD_ROM_FN(SpecDarkredux)

struct BurnDriver BurnSpecDarkredux = {
	"spec_darkredux", NULL, "spec_spec128", NULL, "2021",
	"Dark Redux, The (HB)\0", NULL, "Zosya Entertainment", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDarkreduxRomInfo, SpecDarkreduxRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Krpat (HB)

static struct BurnRomInfo SpecKrpatRomDesc[] = {
	{ "Krpat (2020)(Bizard).tap", 28477, 0xd4b9f078, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKrpat, SpecKrpat, Spec128)
STD_ROM_FN(SpecKrpat)

struct BurnDriver BurnSpecKrpat = {
	"spec_krpat", NULL, "spec_spec128", NULL, "2020",
	"Krpat (HB)\0", NULL, "Bizard", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecKrpatRomInfo, SpecKrpatRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Red Raid - The Sinking, Side A (HB)

static struct BurnRomInfo SpecRedraidsinking1RomDesc[] = {
	{ "Red Raid - The Sinking, Side A (2020)(ZXBitles).tap", 47159, 0x5738cae9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRedraidsinking1, SpecRedraidsinking1, Spec128)
STD_ROM_FN(SpecRedraidsinking1)

struct BurnDriver BurnSpecRedraidsinking1 = {
	"spec_redraidsinking1", NULL, "spec_spec128", NULL, "2020",
	"Red Raid - The Sinking, Side A (HB)\0", NULL, "ZXBitles", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRedraidsinking1RomInfo, SpecRedraidsinking1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Red Raid - The Sinking, Side B (HB)

static struct BurnRomInfo SpecRedraidsinking2RomDesc[] = {
	{ "Red Raid - The Sinking, Side B (2020)(ZXBitles).tap", 47338, 0xc364362c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRedraidsinking2, SpecRedraidsinking2, Spec128)
STD_ROM_FN(SpecRedraidsinking2)

struct BurnDriver BurnSpecRedraidsinking2 = {
	"spec_redraidsinking2", "spec_redraidsinking1", "spec_spec128", NULL, "2020",
	"Red Raid - The Sinking, Side B (HB)\0", NULL, "ZXBitles", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRedraidsinking2RomInfo, SpecRedraidsinking2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Sea Dragon (HB)

static struct BurnRomInfo SpecseadragonRomDesc[] = {
	{ "Sea Dragon (2010)(Andrew Zhiglov)(128k).z80", 0x13b53, 0xf9fe097c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specseadragon, Specseadragon, Spec128)
STD_ROM_FN(Specseadragon)

struct BurnDriver BurnSpecseadragon = {
	"spec_seadragon", NULL, "spec_spec128", NULL, "2010",
	"Sea Dragon (HB)\0", NULL, "Andrew Zhiglov", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecseadragonRomInfo, SpecseadragonRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dark Transit (HB)

static struct BurnRomInfo SpecDarktransitRomDesc[] = {
	{ "Dark Transit (2021)(Sasa Bjedovic).tap", 27345, 0xd963212d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDarktransit, SpecDarktransit, Spec128)
STD_ROM_FN(SpecDarktransit)

struct BurnDriver BurnSpecDarktransit = {
	"spec_darktransit", NULL, "spec_spec128", NULL, "2021",
	"Dark Transit (HB)\0", NULL, "Sasa Bjedovic", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecDarktransitRomInfo, SpecDarktransitRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yanga Plus (HB)

static struct BurnRomInfo SpecYangaplusRomDesc[] = {
	{ "Yanga Plus (2021)(Serdjuk).tap", 55855, 0x4c6d1b08, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYangaplus, SpecYangaplus, Spec128)
STD_ROM_FN(SpecYangaplus)

struct BurnDriver BurnSpecYangaplus = {
	"spec_yangaplus", NULL, "spec_spec128", NULL, "2021",
	"Yanga Plus (HB)\0", NULL, "Serdjuk", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecYangaplusRomInfo, SpecYangaplusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// AntEater (HB)

static struct BurnRomInfo SpecAnteaterRomDesc[] = {
	{ "AntEater (2021)(Defb).tap", 44819, 0x31e117c5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAnteater, SpecAnteater, Spec128)
STD_ROM_FN(SpecAnteater)

struct BurnDriver BurnSpecAnteater = {
	"spec_anteater", NULL, "spec_spec128", NULL, "2021",
	"AntEater (HB)\0", NULL, "Defb", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecAnteaterRomInfo, SpecAnteaterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dark, The - Lost Pages (HB)

static struct BurnRomInfo SpecDarklostpagesRomDesc[] = {
	{ "Dark, The - Lost Pages (2021)(Zosya Entertainment).tap", 55037, 0x4ee940ee, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDarklostpages, SpecDarklostpages, Spec128)
STD_ROM_FN(SpecDarklostpages)

struct BurnDriver BurnSpecDarklostpages = {
	"spec_darklostpages", NULL, "spec_spec128", NULL, "2021",
	"Dark, The - Lost Pages (HB)\0", NULL, "Zosya Entertainment", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MISC, 0,
	SpectrumGetZipName, SpecDarklostpagesRomInfo, SpecDarklostpagesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy 1 Extended (HB)

static struct BurnRomInfo SpecDizzy1extRomDesc[] = {
	{ "Dizzy 1 Extended (2021)(Titus).tap", 38104, 0xd4393e5e, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDizzy1ext, SpecDizzy1ext, Spec128)
STD_ROM_FN(SpecDizzy1ext)

struct BurnDriver BurnSpecDizzy1ext = {
	"spec_dizzy1ext", NULL, "spec_spec128", NULL, "2021",
	"Dizzy 1 Extended (HB)\0", NULL, "Dr. Titus", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecDizzy1extRomInfo, SpecDizzy1extRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dizzy 1 Extended (HB, Russian)

static struct BurnRomInfo SpecDizzy1extrusRomDesc[] = {
	{ "Dizzy 1 Extended (2021)(Titus)(Rus).tap", 38214, 0x9df57a5a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDizzy1extrus, SpecDizzy1extrus, Spec128)
STD_ROM_FN(SpecDizzy1extrus)

struct BurnDriver BurnSpecDizzy1extrus = {
	"spec_dizzy1extrus", "spec_dizzy1ext", "spec_spec128", NULL, "2021",
	"Dizzy 1 Extended (HB, Russian)\0", NULL, "Dr. Titus", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecDizzy1extrusRomInfo, SpecDizzy1extrusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Crystal Kingdom Dizzy - Remake (HB)

static struct BurnRomInfo SpecDizzy7remakeRomDesc[] = {
	{ "Crystal Kindom Dizzy - Remake (2017)(Codemasters).tap", 95390, 0x65df9186, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDizzy7remake, SpecDizzy7remake, Spec128)
STD_ROM_FN(SpecDizzy7remake)

struct BurnDriver BurnSpecDizzy7remake = {
	"spec_dizzy7remake", NULL, "spec_spec128", NULL, "2017",
	"Crystal Kingdom Dizzy - Remake (HB)\0", NULL, "Codemasters", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM | GBF_ADV, 0,
	SpectrumGetZipName, SpecDizzy7remakeRomInfo, SpecDizzy7remakeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lost Cavern (HB)

static struct BurnRomInfo SpecLostcavernRomDesc[] = {
	{ "Lost Cavern (2021)(Isaiasdiaz).tap", 46610, 0x987ec991, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLostcavern, SpecLostcavern, Spec128)
STD_ROM_FN(SpecLostcavern)

struct BurnDriver BurnSpecLostcavern = {
	"spec_lostcavern", NULL, "spec_spec128", NULL, "2021",
	"Lost Cavern (HB)\0", NULL, "Isaiasdiaz", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING| BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecLostcavernRomInfo, SpecLostcavernRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Lost Cavern - Easy (HB)

static struct BurnRomInfo SpecLostcaverneasyRomDesc[] = {
	{ "Lost Cavern - Easy (2021)(Isaiasdiaz).tap", 46585, 0x37e3379b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecLostcaverneasy, SpecLostcaverneasy, Spec128)
STD_ROM_FN(SpecLostcaverneasy)

struct BurnDriver BurnSpecLostcaverneasy = {
	"spec_lostcaverneasy", "spec_lostcavern", "spec_spec128", NULL, "2021",
	"Lost Cavern - Easy (HB)\0", NULL, "Isaiasdiaz", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecLostcaverneasyRomInfo, SpecLostcaverneasyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninja Poison - Part 1 (HB)

static struct BurnRomInfo SpecNinjapoison1RomDesc[] = {
	{ "Ninja Poison - Part 1 (2021)(TokuSoft).tap", 45684, 0x2cd6d37d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNinjapoison1, SpecNinjapoison1, Spec128)
STD_ROM_FN(SpecNinjapoison1)

struct BurnDriver BurnSpecNinjapoison1 = {
	"spec_ninjapoison1", NULL, "spec_spec128", NULL, "2021",
	"Ninja Poison - Part 1 (HB)\0", NULL, "TokuSoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecNinjapoison1RomInfo, SpecNinjapoison1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninja Poison - Part 2 (HB)

static struct BurnRomInfo SpecNinjapoison2RomDesc[] = {
	{ "Ninja Poison - Part 2 (2021)(TokuSoft).tap", 48076, 0xe4cce9b9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNinjapoison2, SpecNinjapoison2, Spec128)
STD_ROM_FN(SpecNinjapoison2)

struct BurnDriver BurnSpecNinjapoison2 = {
	"spec_ninjapoison2", "spec_ninjapoison1", "spec_spec128", NULL, "2021",
	"Ninja Poison - Part 2 (HB)\0", NULL, "TokuSoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecNinjapoison2RomInfo, SpecNinjapoison2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nixy and the Seeds of Doom 128k (HB)

static struct BurnRomInfo SpecnixyseedsofdRomDesc[] = {
	{ "Nixy and the Seeds of Doom (2019)(Bubblesoft)(128k).z80", 0x1114e, 0x81e1a74d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnixyseedsofd, Specnixyseedsofd, Spec128)
STD_ROM_FN(Specnixyseedsofd)

struct BurnDriver BurnSpecnixyseedsofd = {
	"spec_nixyseedsofd", NULL, "spec_spec128", NULL, "2019",
	"Nixy and the Seeds of Doom 128k (HB)\0", NULL, "Bubblesoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecnixyseedsofdRomInfo, SpecnixyseedsofdRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nixy the Glade Sprite 128k (HB)

static struct BurnRomInfo SpecnixyglaspriRomDesc[] = {
	{ "Nixy the Glade Sprite (2018)(Bubblesoft)(128k).z80", 0x1057e, 0x3538816b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specnixyglaspri, Specnixyglaspri, Spec128)
STD_ROM_FN(Specnixyglaspri)

struct BurnDriver BurnSpecnixyglaspri = {
	"spec_nixyglaspri", NULL, "spec_spec128", NULL, "2018",
	"Nixy the Glade Sprite 128k (HB)\0", NULL, "Bubblesoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecnixyglaspriRomInfo, SpecnixyglaspriRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Organic Matter (HB)

static struct BurnRomInfo SpecOrganicmatterRomDesc[] = {
	{ "Organic Matter (2021)(Chentzilla).tap", 38849, 0xc4798ebb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOrganicmatter, SpecOrganicmatter, Spec128)
STD_ROM_FN(SpecOrganicmatter)

struct BurnDriver BurnSpecOrganicmatter = {
	"spec_organicmatter", NULL, "spec_spec128", NULL, "2021",
	"Organic Matter (HB)\0", NULL, "Chentzilla", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecOrganicmatterRomInfo, SpecOrganicmatterRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Maritrini Freelance MonsterSlayer (HB)

static struct BurnRomInfo SpecmaritriniRomDesc[] = {
	{ "Maritrini Freelance MonsterSlayer (2011)(Mojon Twins).tap", 91550, 0xb2a347b9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specmaritrini, Specmaritrini, Spec128)
STD_ROM_FN(Specmaritrini)

struct BurnDriver BurnSpecmaritrini = {
	"spec_maritrini", NULL, "spec_spec128", NULL, "2011",
	"Maritrini Freelance MonsterSlayer (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RUNGUN | GBF_MAZE, 0,
	SpectrumGetZipName, SpecmaritriniRomInfo, SpecmaritriniRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.o.L.o. EN (HB)

static struct BurnRomInfo SpecsoloenRomDesc[] = {
	{ "S.o.L.o (EN)(2021)(Roolandoo).tap", 44386, 0x1db76b41, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsoloen, Specsoloen, Spec128)
STD_ROM_FN(Specsoloen)

struct BurnDriver BurnSpecsoloen = {
	"spec_soloen", NULL, "spec_spec128", NULL, "2021",
	"S.o.L.o. (HB, En)\0", NULL, "Roolandoo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecsoloenRomInfo, SpecsoloenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// S.o.L.o. ES (HB)

static struct BurnRomInfo SpecsoloesRomDesc[] = {
	{ "S.o.L.o (ES)(2021)(Roolandoo).tap", 44386, 0x20ba7c97, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(Specsoloes, Specsoloes, Spec128)
STD_ROM_FN(Specsoloes)

struct BurnDriver BurnSpecsoloes = {
	"spec_soloes", "spec_soloen", "spec_spec128", NULL, "2021",
	"S.o.L.o. (HB, Es)\0", NULL, "Roolandoo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecsoloesRomInfo, SpecsoloesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Drift! (HB)

static struct BurnRomInfo SpecDriftRomDesc[] = {
	{ "Drift! (2019)(Zosya).tap", 128215, 0x35e3f19b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDrift, SpecDrift, Spec128)
STD_ROM_FN(SpecDrift)

struct BurnDriver BurnSpecDrift = {
	"spec_drift", NULL, "spec_spec128", NULL, "2019",
	"Drift! (HB)\0", "Designed esclusively for 128K system", "Zosya", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_RACING, 0,
	SpectrumGetZipName, SpecDriftRomInfo, SpecDriftRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Fase Bonus The Game (HB)

static struct BurnRomInfo SpecFasebonusRomDesc[] = {
	{ "Fase Bonus The Game (2017)(Mojon Twins).tap", 39076, 0x493f3bc8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFasebonus, SpecFasebonus, Spec128)
STD_ROM_FN(SpecFasebonus)

struct BurnDriver BurnSpecFasebonus = {
	"spec_fasebonus", NULL, "spec_spec128", NULL, "2017",
	"Fase Bonus The Game (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecFasebonusRomInfo, SpecFasebonusRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dungeons of Gomilandia (HB)

static struct BurnRomInfo SpecGomilandiaRomDesc[] = {
	{ "Dungeons of Gomilandia (2020)(RetroWorks).z80", 38766, 0x39d5e947, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGomilandia, SpecGomilandia, Spec128)
STD_ROM_FN(SpecGomilandia)

struct BurnDriver BurnSpecGomilandia = {
	"spec_gomilandia", NULL, "spec_spec128", NULL, "2020",
	"Dungeons of Gomilandia (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecGomilandiaRomInfo, SpecGomilandiaRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// MagicAble (HB)

static struct BurnRomInfo SpecMagicableRomDesc[] = {
	{ "MagicAble (2020)(Packobilly).tap", 44795, 0x4ca69d44, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMagicable, SpecMagicable, Spec128)
STD_ROM_FN(SpecMagicable)

struct BurnDriver BurnSpecMagicable = {
	"spec_magicable", NULL, "spec_spec128", NULL, "2020",
	"MagicAble (HB)\0", "Select keyboard as game inputs", "Packobilly", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMagicableRomInfo, SpecMagicableRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mire Mare v1.09

static struct BurnRomInfo SpecMiremareRomDesc[] = {
	{ "Mire Mare v1.09 (2020)(Luca Bordoni).tap", 47329, 0x91db2eb2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMiremare, SpecMiremare, Spec128)
STD_ROM_FN(SpecMiremare)

struct BurnDriver BurnSpecMiremare = {
	"spec_miremare", NULL, "spec_spec128", NULL, "2020",
	"Mire Mare (HB, v1.09)\0", NULL, "Luca Bordoni", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION | GBF_MAZE, 0,
	SpectrumGetZipName, SpecMiremareRomInfo, SpecMiremareRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ramire the Vampire Rewamp Part1 (HB)

static struct BurnRomInfo SpecRamirer1RomDesc[] = {
	{ "Ramire the Vampire Rewamp Part1 (2016)(Mojon Twins).tap", 44159, 0x69c64f56, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRamirer1, SpecRamirer1, Spec128)
STD_ROM_FN(SpecRamirer1)

struct BurnDriver BurnSpecRamirer1 = {
	"spec_ramirer1", NULL, "spec_spec128", NULL, "2016",
	"Ramire the Vampire Rewamp Part-1 (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRamirer1RomInfo, SpecRamirer1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ramire the Vampire Rewamp Part2 (HB)

static struct BurnRomInfo SpecRamirer2RomDesc[] = {
	{ "Ramire the Vampire Rewamp Part2 (2016)(Mojon Twins).tap", 44509, 0x3ce3ae4c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRamirer2, SpecRamirer2, Spec128)
STD_ROM_FN(SpecRamirer2)

struct BurnDriver BurnSpecRamirer2 = {
	"spec_ramirer2", "spec_ramirer1", "spec_spec128", NULL, "2016",
	"Ramire the Vampire Rewamp Part-2 (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRamirer2RomInfo, SpecRamirer2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ramire the Vampire Rewamp Part3 (HB)

static struct BurnRomInfo SpecRamirer3RomDesc[] = {
	{ "Ramire the Vampire Rewamp Part3 (2016)(Mojon Twins).tap", 44938, 0xa65f4290, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRamirer3, SpecRamirer3, Spec128)
STD_ROM_FN(SpecRamirer3)

struct BurnDriver BurnSpecRamirer3 = {
	"spec_ramirer3", "spec_ramirer1", "spec_spec128", NULL, "2016",
	"Ramire the Vampire Rewamp Part-3 (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRamirer3RomInfo, SpecRamirer3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ramiro el Vampiro Rewamp Parte 1 (HB)

static struct BurnRomInfo SpecRamiror1RomDesc[] = {
	{ "Ramiro el Vampiro Rewamp Part1 (2016)(Mojon Twins).tap", 44399, 0x9d9e0971, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRamiror1, SpecRamiror1, Spec128)
STD_ROM_FN(SpecRamiror1)

struct BurnDriver BurnSpecRamiror1 = {
	"spec_ramiror1", NULL, "spec_spec128", NULL, "2016",
	"Ramiro el Vampiro Rewamp Parte 1 (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRamiror1RomInfo, SpecRamiror1RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ramiro el Vampiro Rewamp Parte 2 (HB)

static struct BurnRomInfo SpecRamiror2RomDesc[] = {
	{ "Ramiro el Vampiro Rewamp Part2 (2016)(Mojon Twins).tap", 44601, 0x01ed38e9, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRamiror2, SpecRamiror2, Spec128)
STD_ROM_FN(SpecRamiror2)

struct BurnDriver BurnSpecRamiror2 = {
	"spec_ramiror2", "spec_ramiror1", "spec_spec128", NULL, "2016",
	"Ramiro el Vampiro Rewamp Parte 2 (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRamiror2RomInfo, SpecRamiror2RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ramiro el Vampiro Rewamp Parte 3 (HB)

static struct BurnRomInfo SpecRamiror3RomDesc[] = {
	{ "Ramiro el Vampiro Rewamp Part3 (2016)(Mojon Twins).tap", 45083, 0xec94c119, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRamiror3, SpecRamiror3, Spec128)
STD_ROM_FN(SpecRamiror3)

struct BurnDriver BurnSpecRamiror3 = {
	"spec_ramiror3", "spec_ramiror1", "spec_spec128", NULL, "2016",
	"Ramiro el Vampiro Rewamp Parte 3 (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRamiror3RomInfo, SpecRamiror3RomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Castaway 48K (HB)

static struct BurnRomInfo SpecCastawayRomDesc[] = {
	{ "Castaway 48K (2016)(usebox.net).tap", 29470, 0xa0fccc30, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCastaway, SpecCastaway, Spectrum)
STD_ROM_FN(SpecCastaway)

struct BurnDriver BurnSpecCastaway = {
	"spec_castaway", NULL, "spec_spectrum", NULL, "2016",
	"Castaway 48K (HB)\0", NULL, "usebox.net", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecCastawayRomInfo, SpecCastawayRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Crimbo 128K (HB)

static struct BurnRomInfo SpecCrimboRomDesc[] = {
	{ "Crimbo 128K (2010)(Little Shop of Pixels).tap", 44707, 0x8b519cdf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCrimbo, SpecCrimbo, Spec128)
STD_ROM_FN(SpecCrimbo)

struct BurnDriver BurnSpecCrimbo = {
	"spec_crimbo", NULL, "spec_spec128", NULL, "2010",
	"Crimbo: A Gloop Troops Tale (HB)\0", NULL, "Little Shop of Pixels", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecCrimboRomInfo, SpecCrimboRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// DoubleBubble 128K

static struct BurnRomInfo SpecDbubble128kRomDesc[] = {
	{ "DoubleBubble 128K (2016)(Miguetelo).tap", 36664, 0x3c929681, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDbubble128k, SpecDbubble128k, Spec128)
STD_ROM_FN(SpecDbubble128k)

struct BurnDriver BurnSpecDbubble128k = {
	"spec_dbubble128k", NULL, "spec_spec128", NULL, "2016",
	"DoubleBubble 128K (HB)\0", NULL, "Miguetelo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecDbubble128kRomInfo, SpecDbubble128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// DoubleBubble 48K

static struct BurnRomInfo SpecDbubble48kRomDesc[] = {
	{ "DoubleBubble 48K (2016)(Miguetelo).tap", 33169, 0x07a0f183, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDbubble48k, SpecDbubble48k, Spec128)
STD_ROM_FN(SpecDbubble48k)

struct BurnDriver BurnSpecDbubble48k = {
	"spec_dbubble48k", "spec_dbubble128k", "spec_spec128", NULL, "2016",
	"DoubleBubble 48K (HB)\0", NULL, "Miguetelo", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecDbubble48kRomInfo, SpecDbubble48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Donkey Kong Reloaded (HB)

static struct BurnRomInfo SpecDkreloadRomDesc[] = {
	{ "Donkey Kong Reloaded (2013)(Gabriele Amore).tap", 40960, 0x8000a624, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDkreload, SpecDkreload, Spec128)
STD_ROM_FN(SpecDkreload)

struct BurnDriver BurnSpecDkreload = {
	"spec_dkreload", NULL, "spec_spec128", NULL, "2013",
	"Donkey Kong Reloaded (HB)\0", NULL, "Gabriele Amore", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecDkreloadRomInfo, SpecDkreloadRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gloop Troops 48K (HB)

static struct BurnRomInfo SpecGlooptroopRomDesc[] = {
	{ "Gloop Troops 48K (2011)(Little Shop of Pixels).tap", 45168, 0xee3f924f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGlooptroop, SpecGlooptroop, Spectrum)
STD_ROM_FN(SpecGlooptroop)

struct BurnDriver BurnSpecGlooptroop = {
	"spec_glooptroop", NULL, "spec_spectrum", NULL, "2011",
	"Gloop Troops 48K (HB)\0", NULL, "Little Shop of Pixels", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecGlooptroopRomInfo, SpecGlooptroopRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mabus Mania EN (HB)

static struct BurnRomInfo SpecMabusenRomDesc[] = {
	{ "Mabus Mania EN (2021)(Hicks).tap", 44176, 0x94ae9080, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMabusen, SpecMabusen, Spec128)
STD_ROM_FN(SpecMabusen)

struct BurnDriver BurnSpecMabusen = {
	"spec_mabusen", NULL, "spec_spec128", NULL, "2021",
	"Mabus Mania EN (HB)\0", "English-language version", "Hicks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMabusenRomInfo, SpecMabusenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Mabus Mania ES (HB)

static struct BurnRomInfo SpecMabusesRomDesc[] = {
	{ "Mabus Mania ES (2021)(Hicks).tap", 44200, 0x3d70ab7b, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMabuses, SpecMabuses, Spec128)
STD_ROM_FN(SpecMabuses)

struct BurnDriver BurnSpecMabuses = {
	"spec_mabuses", "spec_mabusen", "spec_spec128", NULL, "2021",
	"Mabus Mania ES (HB)\0", "Spanish-language version", "Hicks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMabusesRomInfo, SpecMabusesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Metamorphosis (HB)

static struct BurnRomInfo SpecMetamorphosisRomDesc[] = {
	{ "Metamorphosis (2021)(ITNL-team).tap", 49194, 0xd22fb315, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMetamorphosis, SpecMetamorphosis, Spec128)
STD_ROM_FN(SpecMetamorphosis)

struct BurnDriver BurnSpecMetamorphosis = {
	"spec_metamorphosis", NULL, "spec_spec128", NULL, "2021",
	"Metamorphosis (HB)\0", NULL, "ITNL-team", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMetamorphosisRomInfo, SpecMetamorphosisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rodman v1.2 (HB)

static struct BurnRomInfo SpecRodmanRomDesc[] = {
	{ "Rodman v1.2 (2018)(TFW8b).tap", 21795, 0xc7ba4663, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRodman, SpecRodman, Spec128)
STD_ROM_FN(SpecRodman)

struct BurnDriver BurnSpecRodman = {
	"spec_rodman", NULL, "spec_spec128", NULL, "2018",
	"Rodman v1.2 (HB)\0", NULL, "The Future Was 8bit", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecRodmanRomInfo, SpecRodmanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Shuttlebug 48K (HB)

static struct BurnRomInfo SpecShuttlebugRomDesc[] = {
	{ "Shuttlebug 48K (2012)( Stonechat).tap", 32148, 0xe7f63317, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecShuttlebug, SpecShuttlebug, Spec128)
STD_ROM_FN(SpecShuttlebug)

struct BurnDriver BurnSpecShuttlebug = {
	"spec_shuttlebug", NULL, "spec_spec128", NULL, "2012",
	"Shuttlebug 48K (HB)\0", NULL, " Stonechat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE, 0,
	SpectrumGetZipName, SpecShuttlebugRomInfo, SpecShuttlebugRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Yumiko in the haunted mansion (HB)

static struct BurnRomInfo SpecYumikohmRomDesc[] = {
	{ "Yumiko in the haunted mansion (2012)(Fun Forge).tap", 30728, 0x2d83ffea, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecYumikohm, SpecYumikohm, Spec128)
STD_ROM_FN(SpecYumikohm)

struct BurnDriver BurnSpecYumikohm = {
	"spec_yumikohm", NULL, "spec_spec128", NULL, "2012",
	"Yumiko in the haunted mansion (HB)\0", NULL, "Fun Forge", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_MAZE | GBF_ADV, 0,
	SpectrumGetZipName, SpecYumikohmRomInfo, SpecYumikohmRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Astro Phobos v1.3 (HB)

static struct BurnRomInfo SpecAstrophobosRomDesc[] = {
	{ "Astro Phobos v1.3 (2021)(goliat).z80", 131151, 0x1acd61a3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecAstrophobos, SpecAstrophobos, Spec128)
STD_ROM_FN(SpecAstrophobos)

struct BurnDriver BurnSpecAstrophobos = {
	"spec_astrophobos", NULL, "spec_spec128", NULL, "2021",
	"Astro Phobos v1.3 (HB)\0", NULL, "Goliat", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecAstrophobosRomInfo, SpecAstrophobosRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cadaverion EN (HB)

static struct BurnRomInfo SpecCadaverionenRomDesc[] = {
	{ "Cadaverion EN (2013)(Mojon Twins).tap", 40573, 0xce3d8b7f, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCadaverionen, SpecCadaverionen, Spectrum)
STD_ROM_FN(SpecCadaverionen)

struct BurnDriver BurnSpecCadaverionen = {
	"spec_cadaverionen", NULL, "spec_spectrum", NULL, "2013",
	"Cadaverion EN (HB)\0", "English-language version", "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ADV | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecCadaverionenRomInfo, SpecCadaverionenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Cadaverion ES (HB)

static struct BurnRomInfo SpecCadaverionesRomDesc[] = {
	{ "Cadaverion ES (2013)(Mojon Twins).tap", 40581, 0x53481358, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCadaveriones, SpecCadaveriones, Spectrum)
STD_ROM_FN(SpecCadaveriones)

struct BurnDriver BurnSpecCadaveriones = {
	"spec_cadaveriones", "spec_cadaverionen", "spec_spectrum", NULL, "2013",
	"Cadaverion ES (HB)\0", "Spanish-language version", "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ADV | GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecCadaverionesRomInfo, SpecCadaverionesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Castle Of Dr Game (HB)

static struct BurnRomInfo SpecCodgRomDesc[] = {
	{ "Castle Of Dr Game (2020)(Paul Jenkinson).tap", 38366, 0x5063f4b5, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCodg, SpecCodg, Spectrum)
STD_ROM_FN(SpecCodg)

struct BurnDriver BurnSpecCodg = {
	"spec_codg", NULL, "spec_spectrum", NULL, "2020",
	"Castle Of Dr Game (HB)\0", NULL, "Paul Jenkinson", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecCodgRomInfo, SpecCodgRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Charm, The (HB)

static struct BurnRomInfo SpecCharmRomDesc[] = {
	{ "The Charm (2014)(RetroWorks).z80", 100884, 0xe56ecd56, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCharm, SpecCharm, Spec128)
STD_ROM_FN(SpecCharm)

struct BurnDriver BurnSpecCharm = {
	"spec_charm", NULL, "spec_spec128", NULL, "2014",
	"Charm, The (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecCharmRomInfo, SpecCharmRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Che-Man (HB)

static struct BurnRomInfo SpecChemanRomDesc[] = {
	{ "Che-Man (2019)(Mojon Twins).tap", 21346, 0xab6e3d27, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecCheman, SpecCheman, Spectrum)
STD_ROM_FN(SpecCheman)

struct BurnDriver BurnSpecCheman = {
	"spec_cheman", NULL, "spec_spectrum", NULL, "2019",
	"Che-Man (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecChemanRomInfo, SpecChemanRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Deep Core Raider (HB, v1.1)

static struct BurnRomInfo SpecDcraiderRomDesc[] = {
	{ "Deep Core Raider v1.1 (2016)(Paul Jenkinson).tap", 39312, 0x3a2bf4cb, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDcraider, SpecDcraider, Spec128)
STD_ROM_FN(SpecDcraider)

struct BurnDriver BurnSpecDcraider = {
	"spec_dcraider", NULL, "spec_spec128", NULL, "2016",
	"Deep Core Raider (HB, v1.1)\0", NULL, "Paul Jenkinson", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecDcraiderRomInfo, SpecDcraiderRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dogmole Tuppowski - The New Adventures (HB)

static struct BurnRomInfo SpecDogadvenRomDesc[] = {
	{ "Dogmole Tuppowski - The New Adventures (2014)(Mojon Twins).tap", 53730, 0x23af5af2, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDogadven, SpecDogadven, Spec128)
STD_ROM_FN(SpecDogadven)

struct BurnDriver BurnSpecDogadven = {
	"spec_dogadven", NULL, "spec_spec128", NULL, "2014",
	"Dogmole Tuppowski - The New Adventures (HB)\0", "English-language version", "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecDogadvenRomInfo, SpecDogadvenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Dogmole Tuppowski - Las Nuevas Aventuras (HB)

static struct BurnRomInfo SpecDogadvesRomDesc[] = {
	{ "Dogmole Tuppowski - Las Nuevas Aventuras (2014)(Mojon Twins).tap", 53802, 0xd599d2db, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecDogadves, SpecDogadves, Spec128)
STD_ROM_FN(SpecDogadves)

struct BurnDriver BurnSpecDogadves = {
	"spec_dogadves", "spec_dogadven", "spec_spec128", NULL, "2014",
	"Dogmole Tuppowski - Las Nuevas Aventuras (HB)\0", "Spanish-language version", "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecDogadvesRomInfo, SpecDogadvesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Factory Daze (HB)

static struct BurnRomInfo SpecFactdazeRomDesc[] = {
	{ "Factory Daze (2009)(Bob Smith).z80", 50298, 0xfa6e3240, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecFactdaze, SpecFactdaze, Spec128)
STD_ROM_FN(SpecFactdaze)

struct BurnDriver BurnSpecFactdaze = {
	"spec_factdaze", NULL, "spec_spec128", NULL, "2009",
	"Factory Daze (HB)\0", NULL, "Bob Smith", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecFactdazeRomInfo, SpecFactdazeRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Genesis dawn of a new day (HB)

static struct BurnRomInfo SpecGenesisRomDesc[] = {
	{ "Genesis dawn of a new day (2010)(RetroWorks).tap", 63025, 0x3a5fb7db, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGenesis, SpecGenesis, Spec128)
STD_ROM_FN(SpecGenesis)

struct BurnDriver BurnSpecGenesis = {
	"spec_genesis", NULL, "spec_spec128", NULL, "2010",
	"Genesis dawn of a new day (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_HORSHOOT, 0,
	SpectrumGetZipName, SpecGenesisRomInfo, SpecGenesisRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gommy Defensor Medieval EN (HB)

static struct BurnRomInfo SpecGommyenRomDesc[] = {
	{ "Gommy Defensor Medieval EN (2009)(RetroWorks).tap", 43940, 0xc2c41f87, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGommyen, SpecGommyen, Spec128)
STD_ROM_FN(SpecGommyen)

struct BurnDriver BurnSpecGommyen = {
	"spec_gommyen", NULL, "spec_spec128", NULL, "2009",
	"Gommy Medieval Defender (HB)\0", "English-language version", "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecGommyenRomInfo, SpecGommyenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Gommy Defensor Medieval ES (HB)

static struct BurnRomInfo SpecGommyesRomDesc[] = {
	{ "Gommy Defensor Medieval ES (2009)(RetroWorks).tap", 43943, 0x53a9e2bf, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecGommyes, SpecGommyes, Spec128)
STD_ROM_FN(SpecGommyes)

struct BurnDriver BurnSpecGommyes = {
	"spec_gommyes", "spec_gommyen", "spec_spec128", NULL, "2009",
	"Gommy Defensor Medieval (HB)\0", "Spanish-language version", "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecGommyesRomInfo, SpecGommyesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Hair Raising Adventures of Mr Hair, the (HB)

static struct BurnRomInfo SpecMrhairRomDesc[] = {
	{ "The Hair Raising Adventures of Mr Hair (2019)(Lee Stevenson).tap", 46747, 0x240d58bc, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMrhair, SpecMrhair, Spec128)
STD_ROM_FN(SpecMrhair)

struct BurnDriver BurnSpecMrhair = {
	"spec_mrhair", NULL, "spec_spec128", NULL, "2019",
	"Hair Raising Adventures of Mr Hair, the (HB)\0", NULL, "Lee Stevenson", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecMrhairRomInfo, SpecMrhairRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Jet Paco 48K (HB)

static struct BurnRomInfo SpecJetpacoRomDesc[] = {
	{ "Jet Paco (2013)(The Mojon Twins).tap", 39655, 0x566a9c20, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecJetpaco, SpecJetpaco, Spectrum)
STD_ROM_FN(SpecJetpaco)

struct BurnDriver BurnSpecJetpaco = {
	"spec_jetpaco", NULL, "spec_spectrum", NULL, "2013",
	"Jet Paco 48K (HB)\0", NULL, "The Mojon Twins", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecJetpacoRomInfo, SpecJetpacoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// King's Valley (HB)

static struct BurnRomInfo SpecKingvalleyRomDesc[] = {
	{ "King's Valley (2009)(RetroWorks).tap", 22083, 0xa9d5816a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecKingvalley, SpecKingvalley, Spec128)
STD_ROM_FN(SpecKingvalley)

struct BurnDriver BurnSpecKingvalley = {
	"spec_kingvalley", NULL, "spec_spec128", NULL, "2009",
	"King's Valley (HB)\0", NULL, "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecKingvalleyRomInfo, SpecKingvalleyRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Majikazo (HB)

static struct BurnRomInfo SpecMajikazoRomDesc[] = {
	{ "Majikazo (2012)(RetroWorks).tap", 41678, 0xc121cf7a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecMajikazo, SpecMajikazo, Spectrum)
STD_ROM_FN(SpecMajikazo)

struct BurnDriver BurnSpecMajikazo = {
	"spec_majikazo", NULL, "spec_spectrum", NULL, "2012",
	"Majikazo (HB)\0", "Adaptation of Lemonize's MSX version", "RetroWorks", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecMajikazoRomInfo, SpecMajikazoRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ninja twins Going to Zedeaks 128K (HB)

static struct BurnRomInfo SpecNinjatwinsRomDesc[] = {
	{ "Ninja twins Going to Zedeaks 128K (2014)(SAM Style).tap", 19337, 0x91a6dbd8, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNinjatwins, SpecNinjatwins, Spec128)
STD_ROM_FN(SpecNinjatwins)

struct BurnDriver BurnSpecNinjatwins = {
	"spec_ninjatwins", NULL, "spec_spec128", NULL, "2014",
	"Ninja twins Going to Zedeaks 128K (HB)\0", NULL, "SAM Style", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecNinjatwinsRomInfo, SpecNinjatwinsRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Nohzdyve (HB)

static struct BurnRomInfo SpecNohzdyveRomDesc[] = {
	{ "Nohzdyve (2018)(Tuckersoft).tap", 13797, 0xee509081, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecNohzdyve, SpecNohzdyve, Spec128)
STD_ROM_FN(SpecNohzdyve)

struct BurnDriver BurnSpecNohzdyve = {
	"spec_nohzdyve", NULL, "spec_spec128", NULL, "2018",
	"Nohzdyve (HB)\0", NULL, "Tuckersoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ACTION, 0,
	SpectrumGetZipName, SpecNohzdyveRomInfo, SpecNohzdyveRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecQAOPSpaceDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Oddi The Viking 128K (HB)

static struct BurnRomInfo SpecOddi128kRomDesc[] = {
	{ "Oddi The Viking 128K (2010)(Digital Brains).z80", 39355, 0x9a22b6ef, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOddi128k, SpecOddi128k, Spec128)
STD_ROM_FN(SpecOddi128k)

struct BurnDriver BurnSpecOddi128k = {
	"spec_oddi128k", NULL, "spec_spec128", NULL, "2010",
	"Oddi The Viking 128K (HB)\0", NULL, "Digital Brains", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecOddi128kRomInfo, SpecOddi128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Oddi The Viking 48K (HB)

static struct BurnRomInfo SpecOddi48kRomDesc[] = {
	{ "Oddi The Viking 48K (2010)(Digital Brains).z80", 31576, 0xc436d54c, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecOddi48k, SpecOddi48k, Spectrum)
STD_ROM_FN(SpecOddi48k)

struct BurnDriver BurnSpecOddi48k = {
	"spec_oddi48k", "spec_oddi128k", "spec_spectrum", NULL, "2010",
	"Oddi The Viking 48K (HB)\0", NULL, "Digital Brains", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecOddi48kRomInfo, SpecOddi48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rabbit in Wonderland 128K (HB)

static struct BurnRomInfo SpecRabbitinw128kRomDesc[] = {
	{ "Rabbit in Wonderland 128K (2015)(Javier Fopiani).tap", 69090, 0xe6d51e0d, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRabbitinw128k, SpecRabbitinw128k, Spec128)
STD_ROM_FN(SpecRabbitinw128k)

struct BurnDriver BurnSpecRabbitinw128k = {
	"spec_rabbitinw128k", NULL, "spec_spec128", NULL, "2015",
	"Rabbit in Wonderland 128K (HB)\0", NULL, "Javier Fopiani", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRabbitinw128kRomInfo, SpecRabbitinw128kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Rabbit in Wonderland 48K (HB)

static struct BurnRomInfo SpecRabbitinw48kRomDesc[] = {
	{ "Rabbit in Wonderland 48K (2015)(Javier Fopiani).tap", 44047, 0x523420f3, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRabbitinw48k, SpecRabbitinw48k, Spectrum)
STD_ROM_FN(SpecRabbitinw48k)

struct BurnDriver BurnSpecRabbitinw48k = {
	"spec_rabbitinw48k", "spec_rabbitinw128k", "spec_spectrum", NULL, "2015",
	"Rabbit in Wonderland 48K (HB)\0", NULL, "Javier Fopiani", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PLATFORM, 0,
	SpectrumGetZipName, SpecRabbitinw48kRomInfo, SpecRabbitinw48kRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	SpecInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Ragnablock (HB)

static struct BurnRomInfo SpecRagnablockRomDesc[] = {
	{ "Ragnablock (2006)(Computer Emuzone).z80", 31321, 0xd23221ec, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecRagnablock, SpecRagnablock, Spec128)
STD_ROM_FN(SpecRagnablock)

struct BurnDriver BurnSpecRagnablock = {
	"spec_ragnablock", NULL, "spec_spec128", NULL, "2006",
	"Ragnablock (HB)\0", NULL, "Computer Emuzone", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_BREAKOUT, 0,
	SpectrumGetZipName, SpecRagnablockRomInfo, SpecRagnablockRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Twinlight 128K (HB)

static struct BurnRomInfo SpecTwinlightRomDesc[] = {
	{ "Twinlight 128K (2017)(Retrosouls).tap", 93039, 0x8d52e8f6, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecTwinlight, SpecTwinlight, Spec128)
STD_ROM_FN(SpecTwinlight)

struct BurnDriver BurnSpecTwinlight = {
	"spec_twinlight", NULL, "spec_spec128", NULL, "2017",
	"Twinlight 128K (HB)\0", NULL, "Retrosouls", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_PUZZLE, 0,
	SpectrumGetZipName, SpecTwinlightRomInfo, SpecTwinlightRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Escape from M.O.N.J.A.S. EN (HB)

static struct BurnRomInfo SpecEscapemonjasenRomDesc[] = {
	{ "Escape from M.O.N.J.A.S. EN (2021)(Rastersoft).tap", 48592, 0x4a03ef89, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEscapemonjasen, SpecEscapemonjasen, Spec128)
STD_ROM_FN(SpecEscapemonjasen)

struct BurnDriver BurnSpecEscapemonjasen = {
	"spec_escapemonjasen", NULL, "spec_spec128", NULL, "2021",
	"Escape from M.O.N.J.A.S. EN (HB)\0", NULL, "Rastersoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, SpecEscapemonjasenRomInfo, SpecEscapemonjasenRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};

// Escape from M.O.N.J.A.S. SP (HB)

static struct BurnRomInfo SpecEscapemonjasesRomDesc[] = {
	{ "Escape from M.O.N.J.A.S. ES (2021)(Rastersoft).tap", 48592, 0x6ef28d8a, BRF_ESS | BRF_PRG },
};

STDROMPICKEXT(SpecEscapemonjases, SpecEscapemonjases, Spec128)
STD_ROM_FN(SpecEscapemonjases)

struct BurnDriver BurnSpecEscapemonjases = {
	"spec_escapemonjases", "spec_escapemonjasen", "spec_spec128", NULL, "2021",
	"Escape from M.O.N.J.A.S. SP (HB)\0", NULL, "Rastersoft", "ZX Spectrum",
	NULL, NULL, NULL, NULL,
	BDF_GAME_WORKING | BDF_CLONE | BDF_HOMEBREW, 1, HARDWARE_SPECTRUM, GBF_ADV, 0,
	SpectrumGetZipName, SpecEscapemonjasesRomInfo, SpecEscapemonjasesRomName, NULL, NULL, NULL, NULL, SpecInputInfo, SpecDIPInfo,
	Spec128KInit, SpecExit, SpecFrame, SpecDraw, SpecScan,
	&SpecRecalc, 0x10, 288, 224, 4, 3
};
