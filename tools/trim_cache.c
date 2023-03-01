// keep only current month on CACHE file

#include <stdio.h>
#include <time.h>

#define MAX_TH_INFO 24 * 32

struct TH_INFO {
  time_t tempo;
  float temperature;
  float humidity;
};

struct TH_INFO th_info[MAX_TH_INFO];

unsigned int th_index = 0;

int main(int argc, char *argv[]) {
  FILE *f;
  char buf[64];

  // read
  f = fopen("CACHE", "rb");
  if (f) {
    th_index = fread(&th_info, 1, sizeof(th_info), f);
    th_index /= sizeof(struct TH_INFO);
    printf("Read entries: %d\n", th_index);
    // close
    fclose(f);
  } else
    printf("Cant open CACHE file\n");

  // get month of last entry
  struct tm *last = localtime((const time_t *)&th_info[th_index - 1].tempo);
  printf("Last entry on %d/%d/%d\n", last->tm_mday, last->tm_mon + 1,
         last->tm_year + 1900);
  unsigned int last_mon=last->tm_mon;

  // find start of last month entries
  unsigned int index, passed;
  for (index = 0; index < th_index; index++) {
    struct tm *entry = localtime((const time_t *)&th_info[index].tempo);
    if (entry->tm_mon == last_mon) {
      break;
    }
    passed++;
  }

  // create
  f = fopen("CACHE.NEW", "w+");
  if (f) {
    fwrite(&th_info[index], sizeof(struct TH_INFO), th_index - passed, f);
    // close
    fclose(f);
  } else
    printf("Cant write output\n");
}