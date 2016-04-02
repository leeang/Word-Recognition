void calc_shelving_coef(void)
{
	shelving_coef.b0 = float_to_fr32(0.465464090346913);
	shelving_coef.b1 = float_to_fr32(-0.775712919663902);
	shelving_coef.b2 = float_to_fr32(0.341636074816722);
	shelving_coef.a1 = float_to_fr32(-0.380949232917015);
	shelving_coef.a2 = float_to_fr32(0.162336478416747);
}

void pre_emphasis(fract32 data[], int arr_length)
{
	fract32 x1 = 0, x2 = 0;
	// input buffer
	fract32 y1 = 0, y2 = 0;
	// output buffer

	int index;
	for (index = 0; index < arr_length; index++) {
		fract32 temp_b0 = mult_fr1x32x32(shelving_coef.b0, data[index]);
		fract32 temp_b1 = mult_fr1x32x32(shelving_coef.b1, x1);
		fract32 temp_b2 = mult_fr1x32x32(shelving_coef.b2, x2);
		fract32 temp_a1 = mult_fr1x32x32(shelving_coef.a1, y1);
		fract32 temp_a2 = mult_fr1x32x32(shelving_coef.a2, y2);

		fract32 temp_b = add_fr1x32(add_fr1x32(temp_b0, temp_b1), temp_b2);
		fract32 temp_a = add_fr1x32(temp_a1, temp_a2);
		fract32 temp = sub_fr1x32(temp_b, temp_a);

		x2 = x1;
		x1 = data[index];		//input

		data[index] = shl_fr1x32(temp, 2);

		y2 = y1;
		y1 = data[index];		//output
	}
}

void calc_hamming_coef(void)
{
	int index;
	float w;
	for (index = 0; index < WINDOW_LENGTH; index++) {
		w = 0.54 - 0.46 * cosf( 2 * PI * (float) index / (float) (WINDOW_LENGTH-1) );
		hamming_coef[index] = float_to_fr32(w);
	}
}

void hamming(fract32 data[])
{
	int index;
	for (index = 0; index < WINDOW_LENGTH; index++) {
		data[index] = mult_fr1x32x32(data[index], hamming_coef[index]);
	}
}

float calc_energy(fract32 data[], int arr_length)
{
	int shift = 9;
	// shift = log_2(arr_length)

	fract32 energy_fr = float_to_fr32(0.0);

	int index;
	for (index = 0; index < arr_length; index++) {
		fract32 temp = mult_fr1x32x32(data[index], data[index]);
		temp = shl_fr1x32(temp, -shift);
		// right shift in case of overflow
		energy_fr = add_fr1x32(energy_fr, temp);
	}

	float energy = fr32_to_float(energy_fr) * (1<<shift);

	return energy;
}

int zc_count(fract32 data[], int arr_length)
{
	int zc_num = 0;

	int index;
	for (index = 1; index < arr_length; index++) {
		if ( IS_ZC(data[index], data[index-1]) ) {
			zc_num += 1;
		}
	}
	return zc_num;
}

void calc_bank_gain(void)
{
	float temp;
	int bank_gain_index = 0;

	int bank_num;
	for (bank_num = 0; bank_num < BANK_NUM; bank_num++) {
		int x_length_inc = bank_border[bank_num+1] - bank_border[bank_num];
		int x_length_dec = bank_border[bank_num+2] - bank_border[bank_num+1];

		int offset = bank_border[bank_num] / 125;

		int index = 1;
		int frequency_x4 = (index+offset) * 125;

		while (frequency_x4 <= bank_border[bank_num+1]) {
			temp = (float)(frequency_x4 - bank_border[bank_num]) / (float)x_length_inc;
			bank_gain[bank_gain_index] = float_to_fr32(temp);
			index++;
			frequency_x4 = (index+offset) * 125;
			bank_gain_index++;
		}

		while( frequency_x4 <= bank_border[bank_num+2] ) {
			temp = 1 - (float)(frequency_x4 - bank_border[bank_num+1]) / (float)x_length_dec;
			bank_gain[bank_gain_index] = float_to_fr32(temp);
			index++;
			frequency_x4 = (index+offset) * 125;
			bank_gain_index++;
		}
	}
}

void mel_filter(fract32 power_fr[], float energy_melband[], int block_exponent)
{
	int scale = 1 << block_exponent*2;

	int bank_num;
	for (bank_num = 0; bank_num < BANK_NUM; bank_num++) {
		int offset = bank_border[bank_num]/125;

		int L = bank_border[bank_num+2]/125 - bank_border[bank_num]/125;

		int bank_gain_offset;
		switch (bank_num) {
			case 0:
				bank_gain_offset = bank_border[0]/125;
				break;
			case 1:
				bank_gain_offset = bank_border[2]/125 - bank_border[0]/125;
				break;
			default:
				bank_gain_offset = bank_border[bank_num+1]/125 + bank_border[bank_num]/125 - bank_border[1]/125 - bank_border[0]/125;
		}

		int index;
		for (index = 0; index < L; index++) {
			fract32 temp = mult_fr1x32x32(power_fr[index+offset+1], bank_gain[index+bank_gain_offset]);
			energy_melband[bank_num] = energy_melband[bank_num] + fr32_to_float(temp);
		}

		energy_melband[bank_num] = energy_melband[bank_num] * scale;
		energy_melband[bank_num] = log10f(energy_melband[bank_num]);
	}
}

void calc_dct_coef(void)
{
	float scale = sqrtf(2.0 / BANK_NUM);

	int feat_num, bank_num;

	for (feat_num = 0; feat_num < FEAT_NUM; feat_num++) {
		for (bank_num = 0; bank_num < BANK_NUM/2; bank_num++) {
			dct_coef[feat_num][bank_num] = cosf(PI * (bank_num+0.5) * (float) feat_num / (float) BANK_NUM) * scale;
		}
	}
}

void discrete_cosine_transform(float energy[], float mfcc_row[])
{
	int feat_num, bank_num;

	for (feat_num = 0; feat_num < FEAT_NUM; feat_num++) {
		float sum = 0;
		for (bank_num = 0; bank_num < BANK_NUM/2; bank_num++) {
			if (feat_num%2) {
				sum += (energy[bank_num] - energy[BANK_NUM-1-bank_num]) * dct_coef[feat_num][bank_num];
			} else {
				sum += (energy[bank_num] + energy[BANK_NUM-1-bank_num]) * dct_coef[feat_num][bank_num];
			}
		}
		mfcc_row[feat_num] = sum;
	}
}

void calc_mfcc(fract32 input_fr[], float mfcc_row[])
{
	int i;

	int block_exponent;
	rfft_fr32(input_fr, fft_spectrum, twiddle_table, TWIDDLE_STRIDE, WINDOW_LENGTH, &block_exponent, DYNAMIC_SCALING);

	fract32 power_spectrum[WINDOW_LENGTH];
	for (i = 0; i < WINDOW_LENGTH; ++i) {
		fract32 absolute = cabs_fr32(fft_spectrum[i]);
		power_spectrum[i] = mult_fr1x32x32(absolute, absolute);
	}

	float energy_melband[BANK_NUM] = {0.0};
	mel_filter(power_spectrum, energy_melband, block_exponent);
	discrete_cosine_transform(energy_melband, mfcc_row);
}

void calc_emis(int obs_length, float obs[][FEAT_NUM], int word_index, float *ptr_to_emis)
{
	int state_index;
	int obs_index;
	int feat_num;

	for (obs_index = 0; obs_index < obs_length; obs_index++) {
		for (state_index = 0; state_index < STATE_NUM; state_index++) {

			float trans = 0;
			for (feat_num = 0; feat_num < FEAT_NUM; feat_num++) {
				float obs_minus_mu = obs[obs_index][feat_num] - mu[word_index][state_index][feat_num];

				trans += obs_minus_mu * inv_Var[word_index][state_index][feat_num] * obs_minus_mu;
			}

			float exp_part = trans;
			// exp_part = exp(-0.5 * trans);
			// take logarithm
			// inv_Var[word_index][state_index][feat_num] has been multiplied by -0.5 in MATLAB before export

			EMIS(obs_index, state_index) = det_part[word_index][state_index] + exp_part;
			// det_part = 1 / ( sqrt(pow((2*PI), FEAT_NUM) * det_var[state_index]) );
			// take logarithm
			// preceding 2 steps have been done in MATLAB before export
			// multiplication becomes addition

		}
	}
}

float viterbi(int obs_length, float obs[][FEAT_NUM], int word_index)
{
	float *ptr_to_emis;
	ptr_to_emis = (float *) calloc(obs_length * STATE_NUM, sizeof(float));

	float *ptr_to_phi;
	ptr_to_phi = (float *) calloc(obs_length * STATE_NUM, sizeof(float));

	calc_emis(obs_length, obs, word_index, ptr_to_emis);

	// initialization
	PHI(0, 0) = EMIS(0, 0);


	int obs_index;
	int statei, statej;

	// recursion
	for (obs_index = 1; obs_index < obs_length; obs_index++) {
		for (statej = 0; statej < STATE_NUM; statej++) {

			float max_phi_plus_trans = -Inf;

			// find max( PHI(obs_index-1, statei) + trans[word_index][statei][statej] ) when statei varies

			// statei = 0;
			// while (statei < STATE_NUM) {
			// optimization considering the shape of trans matrix
			statei = (statej==0) ? 0 : (statej-1);
			while (statei <= statej) {
				float phi_plus_trans = PHI(obs_index-1, statei) + trans[word_index][statei][statej];

				if (phi_plus_trans > max_phi_plus_trans){
					max_phi_plus_trans = phi_plus_trans;
				}
				statei++;
			}

			PHI(obs_index, statej) = max_phi_plus_trans + EMIS(obs_index, statej);

		}
	}

	float probability = -Inf;
	// find max( PHI(obs_length-1, statei) ) when statei changes
	for (statei = 0; statei < STATE_NUM; statei++) {
		if (PHI(obs_length-1, statei) > probability){
			probability = PHI(obs_length-1, statei);
		}
	}

	free(ptr_to_emis);
	free(ptr_to_phi);

	return probability;
}

int get_result(int obs_length, float obs[][FEAT_NUM])
{
	float P_word_max = -Inf;
	int result;

	int word_index;
	for (word_index=0; word_index < WORD_NUM ; word_index++) {
		float P_word = viterbi(obs_length, obs, word_index);

		if (P_word > P_word_max) {
			P_word_max = P_word;
			result = word_index;
		}
		// printf("%f\t", P_word);
	}
	// printf("\n");

	return result;
}
