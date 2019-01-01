/*
Tonemaster - HDR software
Copyright (C) 2018 Stefan Monov <logixoul@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "precompiled.h"
#include "util.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // tmp for AllocConsole

void trapFP()
{
	 // Get the default control word.
   int cw = _controlfp_s(NULL, 0,0 );

   // Set the exception masks OFF, turn exceptions on.
   cw &=~(EM_OVERFLOW|EM_UNDERFLOW|/*EM_INEXACT|*/EM_ZERODIVIDE|EM_DENORMAL);

   // Set the control word.
   _controlfp_s(NULL, cw, MCW_EM );
}

float randFloat()
{
	return rand() / (float)RAND_MAX;
}

void createConsole()
{
	AllocConsole();
	std::fstream* fsOut = new std::fstream("CONOUT$");
	std::cout.rdbuf(fsOut->rdbuf());
	std::fstream* fsIn = new std::fstream("CONIN$");
	std::cin.rdbuf(fsIn->rdbuf());
}

void loadFile(std::vector<unsigned char>& buffer, const std::string& filename) //designed for loading files from hard disk in an std::vector
{
  std::ifstream file(filename.c_str(), std::ios::in|std::ios::binary|std::ios::ate);
  if(!file) throw new runtime_error("file doesn't exist");
  //get filesize
  std::streamsize size = 0;
  if(file.seekg(0, std::ios::end).good()) size = file.tellg();
  if(file.seekg(0, std::ios::beg).good()) size -= file.tellg();

  //read contents of the file into the vector
  if(size > 0)
  {
    buffer.resize((size_t)size);
    file.read((char*)(&buffer[0]), size);
  }
  else buffer.clear();
}

float linearstep(float edge0, float edge1, float x)
{
	// Scale, bias and saturate x to 0..1 range
	x = (x - edge0) / (edge1 - edge0);
	x = constrain(x, 0.f, 1.f);
	return x;
}

void copyCvtData(ci::Surface8u const & surface, Array2D<bytevec3> dst)
{
	forxy(dst) {
		ColorAT<uint8_t> inPixel = surface.getPixel(p);
		dst(p) = bytevec3(inPixel.r, inPixel.g, inPixel.b);
	}
}

void copyCvtData(ci::Surface8u const& surface, Array2D<vec3> dst) {
	forxy(dst) {
		ColorAT<uint8_t> inPixel = surface.getPixel(p);
		dst(p) = vec3(inPixel.r, inPixel.g, inPixel.b) / 255.0f;
	}
}
void copyCvtData(ci::SurfaceT<float> const& surface, Array2D<vec3> dst) {
	forxy(dst) {
		ColorAT<float> inPixel = surface.getPixel(p);
		dst(p) = vec3(inPixel.r, inPixel.g, inPixel.b);
	}
}
void copyCvtData(ci::SurfaceT<float> const& surface, Array2D<float> dst) {
	forxy(dst) {
		ColorAT<float> inPixel = surface.getPixel(p);
		dst(p) = inPixel.r;
	}
}
void copyCvtData(ci::ChannelT<float> const& surface, Array2D<float> dst) {
	forxy(dst) {
		float inPixel = surface.getValue(p);
		dst(p) = inPixel;
	}
}