#ifndef JOS_KERN_SETTINGS_H
#define JOS_KERN_SETTINGS_H

#define USE_BUDDY
#define USE_PSE
#undef  DISABLE_SELF_MAPPING
#define USE_SYSENTER

#undef  LAB1_GRADING
#undef  LAB2_GRADING
#define LAB3_GRADING

#define debug(...) cprintf(__VA_ARGS__)

#endif
