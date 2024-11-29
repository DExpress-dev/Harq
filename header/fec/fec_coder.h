#pragma once

#ifndef FEC_CODER_H_
#define FEC_CODER_H_

#include "../include/rudp_def.h"
#include "../include/rudp_public.h"

#include <map>
#include <vector>
#include <mutex>

#include "../../../header/write_log.h"
#include "matrix.h"

class fec_encoder
{
public:
	fec_encoder();
	~fec_encoder(void);
	void init_encode();
public:
	void add_frames_buffer(frames_buffer* frames_buffer_ptr);
	uint64 get_encoder_size();

public:
	matrix matrix_ptr_;
	int encode_block_n_, encode_block_m_;
	int single_block_size_;

public:
	std::recursive_mutex encode_buffer_lock_;
	std::vector<encode_buffer *> encode_buffer_vector_;
	uint64 current_encode_buffer_size_;

	std::recursive_mutex encode_lock_;
	std::vector<encode_buffer *> encode_vector_;

	void init(int n, int m, int single_block_size);
	void add_encode(unsigned char *buffer, int size, int postion);
	int get_encode_buffer_size();

private:
	void initMatrix (int n, int m);
	matrix_t createMatrix (int *ib, int *ic, int lenb ,int lenc, int n);
	void xorMulArr (unsigned char num, unsigned char *src, unsigned char *dest, int size);
	void dealbuffer (unsigned char **c, unsigned char* buf, int buflen, int offset, int blen);
public:
	void encode_data();
};

struct decode_buffer 
{
	bool has_;
	unsigned char buffer_[FRAMES_BODY_SIZE];
	int		size_;
};
class fec_decoder
{
public:
	fec_decoder();
	~fec_decoder(void);
	
public:
	bool decoder(std::shared_ptr<fec_group_buffer> group_buffer_ptr, std::vector<std::shared_ptr<frames_buffer>> &decoder_frames_vector);

public:
	int decode_block_n_, decode_block_m_;
	int single_block_size_;
	matrix matrix_ptr_;
	void init(int n, int m, int single_block_size);

public:
	std::recursive_mutex decode_lock_;
	std::vector<decode_buffer *> decode_buffer_vector_;
	std::vector<decode_buffer *> decode_vector_;	
	void add_buffer(unsigned char *data, int size, int postion);
	void add_decode(unsigned char *data, int size, int postion);
	void init_decode();

public:
	void get_loss_index_vector(int *ib, int *ic, int *lenb, int *lenc);

private:
	void initMatrix (int n, int m);
	matrix_t createMatrix (int *ib, int *ic, int lenb ,int lenc); 
	void xorMulArr (unsigned char num, unsigned char *src, unsigned char *dest, int size);

	void buildBlock(matrix_t *A, unsigned char **era, int *ic, int *ib, int lenb, int lenc, int blen);
	void fillerase (unsigned char **e, int begin, int end, int n, int m);
public:
	bool decode_data(int total_size);

public:
	ustd::log::write_log* write_log_ptr_ = nullptr;
	void add_log(const int log_type, const char *context, ...);
};

#endif  // FEC_CODER_H_
