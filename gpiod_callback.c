/*
 * ******************************************************************
 * ZYNTHIAN PROJECT: libgpiod callbacks
 * 
 * Implements a callback mechanism for libgpiod
 * 
 * Copyright (C) 2015-2024 Fernando Moyano <jofemodo@zynthian.org>
 *
 * ******************************************************************
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * For a full copy of the GNU General Public License see the LICENSE.txt file.
 * 
 * ******************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gpiod.h>
#include <pthread.h>

#include "gpiod_callback.h"

//-------------------------------------------------------------------
// Variables
//-------------------------------------------------------------------

// GPIO Chip Data Structure
struct gpiod_chip *rpi_chip = NULL;

// Array of callback structures
struct gpiod_callback rpi_gpiod_callbacks[NUM_RPI_PINS];

// Bulk structure for callback lines
struct gpiod_line_bulk cb_line_bulk;

int end_callback_thread_flag = 0;
pthread_t callback_thread_tid;

//-------------------------------------------------------------------
// Initialization & functions
//-------------------------------------------------------------------

int gpiod_init_callbacks() {
	int i;

	// Initialize GPIOD callback data structures
	for (i=0; i<NUM_RPI_PINS; i++) {
		rpi_gpiod_callbacks[i].pin = -1;
		rpi_gpiod_callbacks[i].line = NULL;
		rpi_gpiod_callbacks[i].callback = NULL;
	}

	// Initialize RPI GPIO chip
	rpi_chip = gpiod_chip_open_by_name(RPI_CHIP_NAME);
	if (!rpi_chip) {
		fprintf(stderr, "Can't open GPIOD RPI chip: %s\n", RPI_CHIP_NAME);
		rpi_chip = NULL;
		return 0;
	}
	return 1;
}

int gpiod_line_register_callback(struct gpiod_line *line, void (*callback)(void)) {
	if (line) {
		int pin = gpiod_line_offset(line);
		rpi_gpiod_callbacks[pin].pin = pin;
		rpi_gpiod_callbacks[pin].line = line;
		rpi_gpiod_callbacks[pin].callback = callback;
		return 1;
	}
	return 0;
}

int gpiod_line_unregister_callback(struct gpiod_line *line) {
	if (line) {
		int pin = gpiod_line_offset(line);
		rpi_gpiod_callbacks[pin].pin = -1;
		rpi_gpiod_callbacks[pin].line = NULL;
		rpi_gpiod_callbacks[pin].callback = NULL;
		return 1;
	}
	return 0;
}

void * gpiod_callbacks_thread(void *arg) {
	end_callback_thread_flag = 0;
	struct timespec ts = { 1, 0 };
	struct gpiod_line_bulk event_bulk;
	struct gpiod_line *line;
	struct gpiod_line_event event;
	int ret = 0;
	int pin;
	int i;
	while (!end_callback_thread_flag) {
		ret = gpiod_line_event_wait_bulk(&cb_line_bulk, &ts, &event_bulk);
		if (ret > 0) {
			for (i=0; i<event_bulk.num_lines; i++) {
				line = event_bulk.lines[i];
				gpiod_line_event_read(line, &event);
				pin = gpiod_line_offset(line);
				//if (event.type == GPIOD_LINE_EVENT_RISING_EDGE)
				//fprintf(stderr,"ZynCore->gpiod_callback_thread(): Got event on pin '%d'!\n", pin);
				rpi_gpiod_callbacks[pin].callback();
			}
		} else if (ret < 0) {
			fprintf(stderr, "ZynCore->gpiod_callback_thread(): Error while processing GPIO events!\n");
			break;
		} else {
			//fprintf(stderr, "ZynCore->gpiod_callback_thread(): Event loop timeout...\n");
		}
	}
	fprintf(stderr, "ZynCore->gpiod_callback_thread(): Finished succesfully\n");
}

int gpiod_start_callbacks() {
	int i;
	gpiod_line_bulk_init(&cb_line_bulk);
	for (i=0; i<NUM_RPI_PINS; i++) {
		struct gpiod_line *line = rpi_gpiod_callbacks[i].line;
		if (line) gpiod_line_bulk_add(&cb_line_bulk, line);
	}
	// Start callback thread
	int err = pthread_create(&callback_thread_tid, NULL, &gpiod_callbacks_thread, NULL);
	if (err != 0) {
		fprintf(stderr, "ZynCore->gpiod_start_callbacks: Can't create callback thread :[%s]", strerror(err));
		return 0;
	} else {
		fprintf(stderr, "ZynCore->gpiod_start_callbacks: Callback thread created successfully\n");
		return 1;
	}
	return 0;
}

int gpiod_stop_callbacks() {
	end_callback_thread_flag = 1;
	return 1;
}

int gpiod_restart_callbacks() {
	gpiod_stop_callbacks();
	return gpiod_start_callbacks();
}

//-------------------------------------------------------------------
// Main function
//-------------------------------------------------------------------

void callback_pin() {
	fprintf(stderr,"CALLBACK PIN\n");
}

int _main() {
	int i;
	struct gpiod_line *line;

	int pins[4] = { 17, 27, 5, 6};

	gpiod_init_callbacks();
	for (i=0; i<4; i++) {
		int pin = pins[i];
	 	line = gpiod_chip_get_line(rpi_chip, pin);
	 	if (line) {
	 		int flags = 0;
	 		if (gpiod_line_request_both_edges_events_flags(line, ZYNCORE_CONSUMER, flags)>=0) {
				gpiod_line_register_callback(line, callback_pin);
				fprintf(stderr, "Succesfully registered pin %d for events\n", pin);
			} else {
				fprintf(stderr, "Error while registering pin %d for events\n", pin);
			}
		} else {
			fprintf(stderr, "Error while getting line for pin %d\n", pin);
		}
	}

	gpiod_start_callbacks();

	return 0;
}

//-------------------------------------------------------------------