#include "LPC214x.h"
#include <stdint.h>
#include <stdio.h>
#include "t962.h"
#include "lcd.h"
#include "nvstorage.h"
#include "eeprom.h"
#include "reflow.h"

#include "reflow_profiles.h"

//#define RAMPTEST
//#define PIDTEST

extern uint8_t graphbmp[];

// CHIPQUIK TS391LT50 low-temp lead-free profile
static const profile ts391lt50profile = {
	"CHIPQUIK TS391LT50", {
		// PREHEAT: Target 90°C
		 50, 50, 55,	// 0 - 30s
		 60, 65, 70,	// 30 - 60s
		 75, 80, 85,	// 60 - 90s
		 90, 90, 90,	// 90 - 120s

		// SOAK: Target 130°C, then hold/gentle rise
		 95, 98,102,	// 120 - 150s
		106,110,114,	// 150 - 180s
		118,122,126,	// 180 - 210s
		130,130,130,	// 210 - 240s
		132,134,135,	// 240 - 270s
		136,137,138,	// 270 - 300s

		// RAMP TO PEAK: Enter liquidus at 138°C
		145,152,158,	// 300 - 330s

		// PEAK: Hit 165°C, brief hold
		165,165,165,	// 330-360s

		// CONTROLLED COOLDOWN: Exit liquidus at 138°C
		158,152,145,	// 360-390s

		// RAPID COOLDOWN: Below liquidus, can cool faster
		138,130,120,	// 390-420s
		108, 95, 82,	// 420-450s
		 68, 55,  0,	// 450-480s
	}
};

// SynTECH-LF normal temp lead-free profile
static const profile syntechlfprofile = {
	"AMTECH SYNTECH-LF", {
		 50, 50, 50, 50, 60, 70, 80, 90,100,110,120,130,140,149,158,166, // 0-150s
		175,184,193,201,210,219,230,240,245,240,230,219,212,205,198,191, // Adjust peak from 230 to 249C
		184,177,157,137,117, 97, 77, 57,  0,  0,  0,  0,  0,  0,  0,  0  // 320-470s
	}
};

#ifdef RAMPTEST
// Ramp speed test temp profile
static const profile rampspeed_testprofile = {
	"RAMP SPEED TEST", {
		 50, 50, 50, 50,245,245,245,245,245,245,245,245,245,245,245,245, // 0-150s
		245,245,245,245,245,245,245,245,245, 50, 50, 50, 50, 50, 50, 50, // 160-310s
		 50, 50, 50, 50, 50, 50, 50, 50,  0,  0,  0,  0,  0,  0,  0,  0  // 320-470s
	}
};
#endif

#ifdef PIDTEST
// PID gain adjustment test profile (5% setpoint change)
static const profile pidcontrol_testprofile = {
	"PID CONTROL TEST",	{
		171,171,171,171,171,171,171,171,171,171,171,171,171,171,171,171, // 0-150s
		180,180,180,180,180,180,180,180,171,171,171,171,171,171,171,171, // 160-310s
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0  // 320-470s
	}
};
#endif

// EEPROM profile 1
static ramprofile ee1 = { "CUSTOM #1" };

// EEPROM profile 2
static ramprofile ee2 = { "CUSTOM #2" };

static const profile* profiles[] = {
	&ts391lt50profile,
	&syntechlfprofile,
#ifdef RAMPTEST
	&rampspeed_testprofile,
#endif
#ifdef PIDTEST
	&pidcontrol_testprofile,
#endif
	(profile*) &ee1,
	(profile*) &ee2
};

#define NUMPROFILES (sizeof(profiles) / sizeof(profiles[0]))

// current profile index
static uint8_t profileidx = 0;

static void ByteswapTempProfile(uint16_t* buf) {
	for (int i = 0; i < NUMPROFILETEMPS; i++) {
		uint16_t word = buf[i];
		buf[i] = word >> 8 | word << 8;
	}
}

void Reflow_LoadCustomProfiles(void) {
	EEPROM_Read((uint8_t*)ee1.temperatures, 2, 96);
	ByteswapTempProfile(ee1.temperatures);

	EEPROM_Read((uint8_t*)ee2.temperatures, 128 + 2, 96);
	ByteswapTempProfile(ee2.temperatures);
}

void Reflow_ValidateNV(void) {
	if (NV_GetConfig(REFLOW_BEEP_DONE_LEN) == 255) {
		// Default 1 second beep length
		NV_SetConfig(REFLOW_BEEP_DONE_LEN, 10);
	}

	if (NV_GetConfig(REFLOW_MIN_FAN_SPEED) == 255) {
		// Default fan speed is now 8
		NV_SetConfig(REFLOW_MIN_FAN_SPEED, 8);
	}

	if (NV_GetConfig(REFLOW_BAKE_SETPOINT_H) == 255 || NV_GetConfig(REFLOW_BAKE_SETPOINT_L) == 255) {
		NV_SetConfig(REFLOW_BAKE_SETPOINT_H, SETPOINT_DEFAULT >> 8);
		NV_SetConfig(REFLOW_BAKE_SETPOINT_L, (uint8_t)SETPOINT_DEFAULT);
		printf("Resetting bake setpoint to default.");
	}

	Reflow_SelectProfileIdx(NV_GetConfig(REFLOW_PROFILE));
}

int Reflow_GetProfileIdx(void) {
	return profileidx;
}

int Reflow_SelectProfileIdx(int idx) {
	if (idx < 0) {
		profileidx = (NUMPROFILES - 1);
	} else if(idx >= NUMPROFILES) {
		profileidx = 0;
	} else {
		profileidx = idx;
	}
	NV_SetConfig(REFLOW_PROFILE, profileidx);
	return profileidx;
}

int Reflow_SelectEEProfileIdx(int idx) {
	if (idx == 1) {
		profileidx = (NUMPROFILES - 2);
	} else if (idx == 2) {
		profileidx = (NUMPROFILES - 1);
	}
	return profileidx;
}

int Reflow_GetEEProfileIdx(void) {
	if (profileidx == (NUMPROFILES - 2)) {
		return 1;
	} else 	if (profileidx == (NUMPROFILES - 1)) {
		return 2;
	} else {
		return 0;
	}
}

int Reflow_SaveEEProfile(void) {
	int retval = 0;
	uint8_t offset;
	uint16_t* tempptr;
	if (profileidx == (NUMPROFILES - 2)) {
		offset = 0;
		tempptr = ee1.temperatures;
	} else if (profileidx == (NUMPROFILES - 1)) {
		offset = 128;
		tempptr = ee2.temperatures;
	} else {
		return -1;
	}
	offset += 2; // Skip "magic"
	ByteswapTempProfile(tempptr);

	// Store profile
	retval = EEPROM_Write(offset, (uint8_t*)tempptr, 96);
	ByteswapTempProfile(tempptr);
	return retval;
}

void Reflow_ListProfiles(void) {
	for (int i = 0; i < NUMPROFILES; i++) {
		printf("%d: %s\n", i, profiles[i]->name);
	}
}

const char* Reflow_GetProfileName(void) {
	return profiles[profileidx]->name;
}

uint16_t Reflow_GetSetpointAtIdx(uint8_t idx) {
	if (idx > (NUMPROFILETEMPS - 1)) {
		return 0;
	}
	return profiles[profileidx]->temperatures[idx];
}

void Reflow_SetSetpointAtIdx(uint8_t idx, uint16_t value) {
	if (idx > (NUMPROFILETEMPS - 1)) { return; }
	if (value > SETPOINT_MAX) { return; }

	uint16_t* temp = (uint16_t*) &profiles[profileidx]->temperatures[idx];
	if (temp >= (uint16_t*)0x40000000) {
		*temp = value; // If RAM-based
	}
}

void Reflow_PlotProfile(int highlight) {
	LCD_BMPDisplay(graphbmp, 0, 0);

	// No need to plot first value as it is obscured by Y-axis
	for(int x = 1; x < NUMPROFILETEMPS; x++) {
		int realx = (x << 1) + XAXIS;
		int y = profiles[profileidx]->temperatures[x] / 5;
		y = YAXIS - y;
		LCD_SetPixel(realx, y);

		if (highlight == x) {
			LCD_SetPixel(realx - 1, y - 1);
			LCD_SetPixel(realx + 1, y + 1);
			LCD_SetPixel(realx - 1, y + 1);
			LCD_SetPixel(realx + 1, y - 1);
		}
	}
}

void Reflow_DumpProfile(int profile) {
	if (profile > NUMPROFILES) {
		printf("\nNo profile with id: %d\n", profile);
		return;
	}

	int current = profileidx;
	profileidx = profile;

	for (int i = 0; i < NUMPROFILETEMPS; i++) {
		printf("%4d,", Reflow_GetSetpointAtIdx(i));
		if (i == 15 || i == 31) {
			printf("\n ");
		}
	}
	printf("\n");
	profileidx = current;
}
