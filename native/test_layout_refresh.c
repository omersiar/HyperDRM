#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "target.h"
#include "layout.h"
#include "diagnostics.h"
static unsigned led_count;
static LedBlend *led_blends;
#include "layout_refresh.h"
static void completed(unsigned count,unsigned generation){
 layout_probe.mapping=calloc(count,sizeof(LedBlend));assert(layout_probe.mapping);
 for(unsigned i=0;i<count;i++)layout_probe.mapping[i]=layout_blend((i+.5)/count,0);
 layout_probe.leds=count;layout_probe.stage=TEST_DONE;layout_probe_generation=generation;
}
int main(void){
 (void)target_connect;(void)target_load;(void)target_save;
 PlaybackJson json={0};assert(playback_json_init(&json));
 calibration.kind=CAL_G4;next_layout_check=UINT64_MAX;
 completed(50,calibration_generation);layout_refresh_tick(&json,1000,true);
 assert(led_count==50 && layout_updates==1 && last_layout_check==1000);
 completed(500,calibration_generation);layout_refresh_tick(&json,2000,true);
 assert(led_count==500 && layout_updates==2 && last_layout_check==2000);
 completed(500,calibration_generation);layout_refresh_tick(&json,3000,true);assert(layout_updates==2);
 completed(500,calibration_generation);layout_probe.mapping[42].weight^=512;
 layout_refresh_tick(&json,4000,true);assert(layout_updates==3);
 LedBlend *previous=led_blends;layout_probe.stage=TEST_FAILED;strcpy(layout_probe.error,"Invalid layout");
 layout_refresh_tick(&json,5000,true);assert(led_blends==previous && led_count==500 && layout_updates==3 && layout_error[0]);
 completed(50,calibration_generation);calibration_generation++;
 layout_refresh_tick(&json,6000,true);assert(led_count==500 && led_blends==previous);assert(probe_busy(&layout_probe));
 layout_refresh_tick(&json,6100,false);assert(!probe_busy(&layout_probe) && layout_probe.fd==-1);
 assert(led_count==500 && led_blends==previous);
 layout_refresh_reset(7000);free(led_blends);test_close();
 puts("PASS: automatic 50/500 LED replacement, same-count position changes, unchanged layout, invalid reply retention, obsolete profile result and cancellation");return 0;
}
