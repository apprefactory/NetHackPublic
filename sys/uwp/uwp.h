/* NetHack 3.6	uwp.h	$NHDT-Date:  $  $NHDT-Branch:  $:$NHDT-Revision:  $ */
/* Copyright (c) Bart House, 2016-2017. */
/* Nethack for the Universal Windows Platform (UWP) */
/* NetHack may be freely redistributed.  See license for details. */

#pragma once

#define ESCAPE 27

#include "uwpenum.h"
#include "uwpglobals.h"
#include "uwpcellbuffer.h"
#include "uwptextgrid.h"
#include "uwpoption.h"
#include "uwpfilehandler.h"
#include "uwputil.h"
#include "uwpeventqueue.h"
#include "uwplock.h"
#include "uwpconditionvariable.h"
#include "uwpmath.h"

extern"C" {

    extern char MapScanCode(const Nethack::Event & e);
    extern int raw_getchar();

}
