// 
//   Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "check.h"

#include <iostream>
#include <sstream>
#include <cassert>
#include <boost/cstdint.hpp>

using namespace std;

int
main(int /*argc*/, char** /*argv*/)
{

	// Check typedef sizes.
	check_equals (sizeof(uint8_t), 1);
	check_equals (sizeof(uint16_t), 2);
	check_equals (sizeof(uint32_t), 4);
	check_equals (sizeof(uint64_t), 8);
	check_equals (sizeof(int8_t), 1);
	check_equals (sizeof(int16_t), 2);
	check_equals (sizeof(int32_t), 4);
	check_equals (sizeof(int64_t), 8);

}

