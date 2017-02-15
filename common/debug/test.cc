
#include "stack_trace.h"

void bar(StackTrace* o) { 
  PrintToStderr("\ndump stack frames from `bar`\n");
  StackTrace s;
  s.Print();

  PrintToStderr("\ndump stack frames with `o`\n");
  o->Print();
}

void foo(StackTrace* o) { 
  PrintToStderr("\ndump stack frames from `foo`\n");
  StackTrace s;
  s.Print();

  bar(o);
}

int main() {
  StackTrace o;
  foo(&o);

  return 0;
}
