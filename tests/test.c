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
