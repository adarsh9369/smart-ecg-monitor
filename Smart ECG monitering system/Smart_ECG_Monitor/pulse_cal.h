float H_th_e = 1;
float L_th_e = -1;
const int buff_size_th_e = 300;
float array_th_e[buff_size_th_e];
int m_e = 0;
int p_e = 0, heart_rate;
bool val_e;
int pr_e;
float sum_e;
unsigned long t_pr_e = 0;
const int arry_size_e = 300;
float array_pulse_e[arry_size_e];

void hr_calculation(float filter_pulse_e)  // filter
{
  array_th_e[m_e] = filter_pulse_e;
  m_e = m_e + 1;
  if (m_e >= buff_size_th_e) {
    m_e = 0;
    H_th_e = 0;
    for (int i = 0; i < buff_size_th_e; ++i) {
      H_th_e = max(H_th_e, array_th_e[i]);
    }
    H_th_e = H_th_e - (H_th_e * 0.2);
  }

  //  L_th = -(H_th - 10); // Low level trigger point
  L_th_e = H_th_e * 0.33;
  if (filter_pulse_e > H_th_e && val_e == 0) {
    pr_e = 60000 / (millis() - t_pr_e);
    val_e = 1;
    t_pr_e = millis();
  }

  else if (filter_pulse_e < L_th_e && val_e == 1) {
    val_e = 0;
  }
  sum_e = sum_e - array_pulse_e[p_e];
  array_pulse_e[p_e] = pr_e;
  sum_e = sum_e + array_pulse_e[p_e];
  heart_rate = (sum_e / arry_size_e);

  if ((heart_rate > 5) && (heart_rate < 300)) {
    heart_rate = heart_rate+1;
  } else {
    heart_rate = 0;
  }
  p_e = p_e + 1;
  if (p_e >= arry_size_e) {
    p_e = 0;
  }
}
