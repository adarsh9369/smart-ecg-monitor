#include "ecg.h"

void MyfilterFilter_init(MyfilterFilter* f)
{
  int i;
  for (i = 0; i < MYFILTERFILTER_TAP_NUM; ++i)
    f->history[i] = 0;
  f->last_index = 0;
}

void MyfilterFilter_put(MyfilterFilter* f, double input)
{
  f->history[f->last_index++] = input;
  if (f->last_index == MYFILTERFILTER_TAP_NUM)
    f->last_index = 0;
}

double MyfilterFilter_get(MyfilterFilter* f)
{
  double acc = 0;
  int index = f->last_index, i;
  for (i = 0; i < MYFILTERFILTER_TAP_NUM; ++i)
  {
    index = index != 0 ? index - 1 : MYFILTERFILTER_TAP_NUM - 1;
    acc += f->history[index] * filter_taps[i];
  };
  return acc;
}
