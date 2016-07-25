/*
 * This defines _BASE as a symbol with address 0.  In a DSO, that means
 * the lowest address in the image, so a PC-relative reference to this
 * symbol resolves to the base address at runtime.
 */
PROVIDE_HIDDEN(_BASE = 0);
