const int arry_size_hr = 100;
float array_data_hr[arry_size_hr];
int j_hr = 0;
double sum_hr = 0.0;

float hr_avg(float hr) {
  sum_hr = sum_hr - array_data_hr[j_hr];
  array_data_hr[j_hr] = hr;
  sum_hr = sum_hr + array_data_hr[j_hr];
  float out_value_hr = (sum_hr / arry_size_hr);
  j_hr = j_hr + 1;
  if (j_hr >= arry_size_hr) {
    j_hr = 0;
  }
  return out_value_hr;
}
