#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "config.h"
#include "settings.h"

// This is an example game entry
//ID=GAFE
//Name=Animal Crossing (NTSC)
//Comment=Playable without issues
//Status=Working
//Patch Type=Low
//Patch Location=Low
//Disable Interrupts=Yes
//Force Video Mode=Progressive
//Mute Audio Streaming=Yes
//Mute Audio Stutter=Yes
//No Disc Mode=Yes

static int configInit = 0;
static ConfigEntry configEntries[2048]; // That's a lot of Games!
static int configEntriesCount = 0;

void strnscpy(char *s1, char *s2, int num) {
	strncpy(s1, s2, num);
	s1[num] = 0;
}

/** 
	Initialises the configuration file
	Returns 1 on successful file open, 0 otherwise
*/
int config_init() {
	
	if(!configInit) {
		sprintf(txtbuffer, "%sswiss.ini", deviceHandler_initial->name);
		FILE *fp = fopen(txtbuffer, "rb");
		if (fp) {
			fseek(fp, 0, SEEK_END);
			int size = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			if (size > 0) {
				char *configData = (char*) memalign(32, size);
				if (configData) {
					fread(configData, 1, size, fp);
					config_parse(configData);
				}
			}
			else {
				return 0;
			}
			fclose(fp);
			return 1;
		}
		else {
			return 0;
		}
	}
	return 1;
}

/**
	Creates a configuration file on the root of the device "swiss.ini"
	Returns 1 on successful creation, 0 otherwise
*/
int config_create() {
	sprintf(txtbuffer, "%sswiss.ini", deviceHandler_initial->name);
	FILE *fp = fopen( txtbuffer, "wb" );
	if(fp) {
		char *str = "// Swiss Configuration File!\r\n\0";
		fwrite(str, 1, strlen(str), fp);
		fclose(fp);
		return 1;
	}
	return 0;
}

int config_update_file() {
	sprintf(txtbuffer, "%sswiss.ini", deviceHandler_initial->name);
	FILE *fp = fopen( txtbuffer, "wb" );
	if(fp) {
		char *str = "//Swiss Configuration File!\r\n//Anything written in here will be lost!\r\n\r\n";
		fwrite(str, 1, strlen(str), fp);
		int i;
		for(i = 0; i < configEntriesCount; i++) {
			char buffer[256];
			strnscpy(buffer, &configEntries[i].game_id[0], 4);
			sprintf(txtbuffer, "ID=%s\r\n",buffer);
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			strnscpy(buffer, &configEntries[i].game_name[0], 32);
			sprintf(txtbuffer, "Name=%s\r\n",buffer);
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			strnscpy(buffer, &configEntries[i].comment[0], 128);
			sprintf(txtbuffer, "Comment=%s\r\n",buffer);
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			strnscpy(buffer, &configEntries[i].status[0], 32);
			sprintf(txtbuffer, "Status=%s\r\n",buffer);
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			sprintf(txtbuffer, "Patch Type=%s\r\n",(configEntries[i].useHiLevelPatch ? "High":"Low"));
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);

			sprintf(txtbuffer, "Patch Location=%s\r\n",(configEntries[i].useHiMemArea ? "High":"Low"));
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			sprintf(txtbuffer, "Disable Interrupts=%s\r\n",(configEntries[i].disableInterrupts ? "Yes":"No"));
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);

			sprintf(txtbuffer, "Force Video Mode=%s\r\n",uiVModeStr[configEntries[i].gameVMode]);
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			sprintf(txtbuffer, "Mute Audio Streaming=%s\r\n",(configEntries[i].muteAudioStreaming ? "Yes":"No"));
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			sprintf(txtbuffer, "Mute Audio Stutter=%s\r\n",(configEntries[i].muteAudioStutter ? "Yes":"No"));
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
			
			sprintf(txtbuffer, "No Disc Mode=%s\r\n\r\n\r\n",(configEntries[i].noDiscMode ? "Yes":"No"));
			fwrite(txtbuffer, 1, strlen(txtbuffer), fp);
		}
		fclose(fp);
		return 1;
	}
	return 0;
}

void config_parse(char *configData) {
	// Parse each entry and put it into our array
	char *line, *linectx;
	int first = 1;
	line = strtok_r( configData, "\r\n", &linectx );
	while( line != NULL ) {
		// sprintf(txtbuffer, "Line [%s]\r\n", line);
		// print_gecko(txtbuffer);
		if(line[0] != '#') {
			// Is this line a new game entry?
			char *name, *namectx;
			char *value = NULL;
			name = strtok_r(line, "=", &namectx);
			if(name != NULL)
				value = strtok_r(NULL, "=", &namectx);
			
			if(value != NULL) {
				//sprintf(txtbuffer, "Name [%s] Value [%s]\r\n", name, value);
				//print_gecko(txtbuffer);

				if(!strcmp("ID", name)) {
					if(!first) {
						configEntriesCount++;
					}
					strncpy(&configEntries[configEntriesCount].game_id[0], value, 4);
					first = 0;
					// Fill this entry with defaults incase some values are missing..
					strcpy(&configEntries[configEntriesCount].comment[0],"No Comment");
					strcpy(&configEntries[configEntriesCount].status[0],"Unknown");
					configEntries[configEntriesCount].useHiLevelPatch = 0;
					configEntries[configEntriesCount].useHiMemArea = 0;
					configEntries[configEntriesCount].disableInterrupts = 1;
					configEntries[configEntriesCount].gameVMode = 3;
					configEntries[configEntriesCount].muteAudioStreaming = 1;
					configEntries[configEntriesCount].muteAudioStutter = 0;
					configEntries[configEntriesCount].noDiscMode = 0;
				}
				else if(!strcmp("Name", name)) {
					strncpy(&configEntries[configEntriesCount].game_name[0], value, 64);
				}
				else if(!strcmp("Comment", name)) {
					strncpy(&configEntries[configEntriesCount].comment[0], value, 128);
				}
				else if(!strcmp("Status", name)) {
					strncpy(&configEntries[configEntriesCount].status[0], value, 32);
				}
				else if(!strcmp("Patch Type", name)) {
					configEntries[configEntriesCount].useHiLevelPatch = !strcmp("Low", value) ? 0:1;
				}
				else if(!strcmp("Patch Location", name)) {
					configEntries[configEntriesCount].useHiMemArea = !strcmp("Low", value) ? 0:1;
				}
				else if(!strcmp("Disable Interrupts", name)) {
					configEntries[configEntriesCount].disableInterrupts = !strcmp("Yes", value) ? 1:0;
				}
				else if(!strcmp("Force Video Mode", name)) {
					if(!strcmp(uiVModeStr[0], value))
						configEntries[configEntriesCount].gameVMode = 0;
					else if(!strcmp(uiVModeStr[1], value))
						configEntries[configEntriesCount].gameVMode = 1;
					else if(!strcmp(uiVModeStr[2], value))
						configEntries[configEntriesCount].gameVMode = 2;
					else if(!strcmp(uiVModeStr[3], value))
						configEntries[configEntriesCount].gameVMode = 3;
				}
				else if(!strcmp("Mute Audio Streaming", name)) {
					configEntries[configEntriesCount].muteAudioStreaming = !strcmp("Yes", value) ? 1:0;
				}
				else if(!strcmp("Mute Audio Stutter", name)) {
					configEntries[configEntriesCount].muteAudioStutter = !strcmp("Yes", value) ? 1:0;
				}
				else if(!strcmp("No Disc Mode", name)) {
					configEntries[configEntriesCount].noDiscMode = !strcmp("Yes", value) ? 1:0;
				}
			}
		}
		// And round we go again
		line = strtok_r( NULL, "\r\n", &linectx);
	}
	free(configData);
	if(configEntriesCount > 0)
		configEntriesCount++;
	
	// sprintf(txtbuffer, "Found %i entries in the config file\r\n",configEntriesCount);
	// print_gecko(txtbuffer);
}

void config_find(ConfigEntry *entry) {
	sprintf(txtbuffer, "config_find: Looking for game with ID %s\r\n",entry->game_id);
	print_gecko(txtbuffer);
	// Try to lookup this game based on game_id
	int i;
	for(i = 0; i < configEntriesCount; i++) {
		if(!strncmp(entry->game_id, configEntries[i].game_id, 4)) {
			memcpy(entry, &configEntries[i], sizeof(ConfigEntry));
			sprintf(txtbuffer, "config_find: Found %s\r\n",entry->game_id);
			print_gecko(txtbuffer);
			return;
		}
	}
	// Didn't find it, setup defaults and add this entry
	strcpy(entry->comment,"No Comment");
	strcpy(entry->status,"Unknown");
	entry->useHiLevelPatch = 0;
	entry->useHiMemArea = 0;
	entry->disableInterrupts = 1;
	entry->gameVMode = 3;
	entry->muteAudioStreaming = 1;
	entry->muteAudioStutter = 0;
	entry->noDiscMode = 0;
	// Add this new entry to our collection
	memcpy(&configEntries[configEntriesCount], entry, sizeof(ConfigEntry));
	configEntriesCount++;
	sprintf(txtbuffer, "config_find: Couldn't find, creating %s\r\n",entry->game_id);
	print_gecko(txtbuffer);
}

int config_update(ConfigEntry *entry) {
	sprintf(txtbuffer, "config_update: Looking for game with ID %s\r\n",entry->game_id);
	print_gecko(txtbuffer);
	int i;
	for(i = 0; i < configEntriesCount; i++) {
		if(!strncmp(entry->game_id, configEntries[i].game_id, 4)) {
			sprintf(txtbuffer, "config_update: Found %s\r\n",entry->game_id);
			print_gecko(txtbuffer);
			memcpy(&configEntries[i], entry, sizeof(ConfigEntry));
			return config_update_file();	// Write out the file now
		}
	}
	return 0; // should never happen since we add in the game
}

int config_get_count() {
	return configEntriesCount;
}

