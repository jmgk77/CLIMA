// dump all content of CACHE file to a CSV

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

  // readd
  f = fopen(argv[2] ? argv[2]:"CACHE", "rb");
  if (f) {
    th_index = fread(&th_info, 1, sizeof(th_info), f);
    th_index /= sizeof(struct TH_INFO);
    printf("Read entries: %d\n", th_index);
    // close
    fclose(f);
  } else
    printf("Cant open CACHE file\n");

  // create
  f = fopen(argv[1], "w+");
  if (f) {
    // CSV header
    fprintf(f, "Hora, Data, Temperatura, Humidade\n");
    // loop database
    for (unsigned int i = 0; i < th_index; i++) {
      // write
      strftime(buf, sizeof(buf), "%T, %d-%m-%Y", localtime(&th_info[i].tempo));
      fprintf(f, "%s, %.01f, %.01f\n", buf, th_info[i].temperature,
              th_info[i].humidity);
    }
    // close
    fclose(f);
  } else
    printf("Cant write output\n");
}