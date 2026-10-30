#ifndef MYFILTERFILTER_H_
#define MYFILTERFILTER_H_

#define MYFILTERFILTER_TAP_NUM 54

typedef struct
{
  double history[MYFILTERFILTER_TAP_NUM];
  unsigned int last_index;
} MyfilterFilter;

void MyfilterFilter_init(MyfilterFilter* f);
void MyfilterFilter_put(MyfilterFilter* f, double input);
double MyfilterFilter_get(MyfilterFilter* f);

/*
fS = 100  # Sampling rate.
fL = 0.56  # Cutoff frequency.
fH = 35  # Cutoff frequency.
NL = 60  # Filter length for roll-off at fL, must be odd.
NH = 60  # Filter length for roll-off at fH, must be odd.
*/

static double filter_taps[MYFILTERFILTER_TAP_NUM] = {
  -0.00189045, -0.00247656, -0.00316661, -0.00396743, -0.00488446, -0.00592121,
  -0.00707892, -0.00835623, -0.00974885, -0.01124945, -0.01284753, -0.01452943,
  -0.01627849, -0.01807523, -0.01989768, -0.02172183, -0.02352205, -0.02527173,
  -0.02694384, -0.02851157, -0.02963022, -0.03531451, -0.05029674, -0.04088928,
  0.05822751, 0.22731008, 0.31627927, 0.22707199, 0.05775569, -0.04158627,
  -0.05120632, -0.03642044, -0.03091301, -0.02994902, -0.02851157, -0.02694384,
  -0.02527173, -0.02352205, -0.02172183, -0.01989768, -0.01807523, -0.01627849,
  -0.01452943, -0.01284753, -0.01124945, -0.00974885, -0.00835623, -0.00707892,
  -0.00592121, -0.00488446, -0.00396743, -0.00316661, -0.00247656, -0.00189045
};

#endif
