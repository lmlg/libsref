/* Test entry point.

   This file is part of libsref.

   libsref is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include "utils.h"
#include "rcu.h"

int main ()
{
  if (sref_lib_init () < 0)
    abort ();

  test_init ();
  const TestModule *mods[] = { &RCU };

  for (size_t i = 0; i < ARRAY_SIZE (mods); ++i)
    {
      const TestModule *mod = mods[i];
      if (mod)
        test_module_run (mod);
    }

  return (0);
}
