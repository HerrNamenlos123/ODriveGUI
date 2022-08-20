#pragma once

#include "pch.h"
#include "OpenSansFontData.h"	// Here an OpenSans font is loaded, this header was auto-generated 
								// from a .ttf file using the Battery::ImGuiUtils::... functions

#include "FontData-Roboto-Medium.h"

#define __ESC(x) x
#define FONT(symbolName, size) Battery::ImGuiUtils::AddEmbeddedFont(__ESC(symbolName)_compressed_data, __ESC(symbolName)_compressed_size, size);

struct FontContainer : public Battery::FontContainer {

	// Here you can load any number of fonts to be used throughout the application
	ImFont* openSans18 = FONT(OpenSansFontData, 18);
	ImFont* openSans21 = FONT(OpenSansFontData, 21);
	ImFont* openSans25 = FONT(OpenSansFontData, 25);
	ImFont* robotoMedium = FONT(RobotoMedium, 18);
	
};
