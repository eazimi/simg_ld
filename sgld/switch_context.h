#ifndef SWITCH_CONTEXT_H
#define SWITCH_CONTEXT_H

/* Code courtesy: Rohan's MANA work */
// Helper class to save and restore context (in particular, the FS register),
// when switching between the upper half and the lower half. In the current
// design, the caller would generally be the upper half, trying to jump into
// the lower half. An example would be calling a real function defined in the
// lower half from a function wrapper defined in the upper half.
// Example usage:
//   int function_wrapper()
//   {
//     SwitchContext ctx;
//     return _real_function();
//   }
// The idea is to leverage the C++ language semantics to help us automatically
// restore the context when the object goes out of scope.
class SwitchContext {
private:
  unsigned long upperHalfFs;
  unsigned long lowerHalfFs;
  int jumped;

public:
  explicit SwitchContext(unsigned long);
  ~SwitchContext();
};

// Helper macro to be used whenever making a jump from the upper half to
// the lower half.
#define JUMP_TO_LOWER_HALF(lhFs)                                                                                       \
  do {                                                                                                                 \
  SwitchContext dmtcp_ctx((unsigned long)lhFs)

// Helper macro to be used whenever making a returning from the lower half to
// the upper half.
#define RETURN_TO_UPPER_HALF()                                                                                         \
  }                                                                                                                    \
  while (0)

// This function splits the process by initializing the lower half with the
// proxy code. It returns 0 on success.
extern int splitProcess();

#endif // ifndef SWITCH_CONTEXT_H
