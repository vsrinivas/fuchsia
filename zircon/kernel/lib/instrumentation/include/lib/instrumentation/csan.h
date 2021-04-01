extern void kcsan_disable_percpu();
extern void kcsan_enable_percpu();

#define data_race_begin	kcsan_disable_percpu
#define data_race_end	kcsan_enable_percpu


