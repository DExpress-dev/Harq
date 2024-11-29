
#include "fec_coder.h"

#define GET_BLOCK_SIZE(_SIZE, _NUM) ((_SIZE - (_SIZE % _NUM)) / _NUM)
const int BUF_LEN	= 256;

fec_encoder::fec_encoder()
{
	current_encode_buffer_size_ = 0;
	init(FEC_GROUP_SIZE, FEC_REDUNDANCY_SIZE, FRAMES_BODY_SIZE);
}

fec_encoder::~fec_encoder(void)
{
	std::lock_guard<std::recursive_mutex> encode_buffer_gurad(encode_buffer_lock_);
	for(auto iter = encode_buffer_vector_.begin(); iter != encode_buffer_vector_.end(); iter++)
	{
		encode_buffer *tmp_encode_buffer = *iter;
		delete tmp_encode_buffer;
	}
	encode_buffer_vector_.clear();

	std::lock_guard<std::recursive_mutex> encode_gurad(encode_lock_);
	for(auto iter = encode_vector_.begin(); iter != encode_vector_.end(); iter++)
	{
		encode_buffer *tmp_encode_buffer = *iter;
		delete tmp_encode_buffer;
	}
	encode_vector_.clear();
}

void fec_encoder::init_encode()
{
	std::lock_guard<std::recursive_mutex> encode_buffer_gurad(encode_buffer_lock_);
	for(auto iter = encode_buffer_vector_.begin(); iter != encode_buffer_vector_.end(); iter++)
	{
		encode_buffer *tmp_encode_buffer = *iter;
		memset(tmp_encode_buffer, 0, sizeof(encode_buffer));
	}
	current_encode_buffer_size_ = 0;

	std::lock_guard<std::recursive_mutex> encode_gurad(encode_lock_);
	for(auto iter = encode_vector_.begin(); iter != encode_vector_.end(); iter++)
	{
		encode_buffer *tmp_encode_buffer = *iter;
		memset(tmp_encode_buffer, 0, sizeof(encode_buffer));
	}
}

uint64 fec_encoder::get_encoder_size()
{
	return current_encode_buffer_size_;
}

void fec_encoder::add_frames_buffer(frames_buffer* frames_buffer_ptr)
{
	uint64 groupid, postion;
	ustd::rudp_public::get_groupid_postion(frames_buffer_ptr->header_.index_, groupid, postion);

	add_encode((unsigned char*)&frames_buffer_ptr->body_, sizeof(frames_buffer_ptr->body_), postion);
}

void fec_encoder::init(int n, int m, int single_block_size)
{
	matrix_ptr_.galois8bit_ptr_.galoisEightBitInit();
	encode_block_n_ = n;
	encode_block_m_ = m;
	single_block_size_ = single_block_size;
	std::lock_guard<std::recursive_mutex> encode_buffer_gurad(encode_buffer_lock_);
	encode_buffer_vector_.reserve(n);

	for(int i = 0; i < n; i++)
	{
		encode_buffer *tmp_encode_buffer = new encode_buffer;
		memset(tmp_encode_buffer, 0, sizeof(encode_buffer));
		tmp_encode_buffer->size_ = 0;
		tmp_encode_buffer->has_ = false;
		encode_buffer_vector_.push_back(tmp_encode_buffer);
	}
	current_encode_buffer_size_ = 0;

	std::lock_guard<std::recursive_mutex> encode_gurad(encode_lock_);
	encode_vector_.reserve(m);
	for(int i = 0; i < m; i++)
	{
		encode_buffer *tmp_encode_buffer = new encode_buffer;
		memset(tmp_encode_buffer, 0, sizeof(encode_buffer));
		tmp_encode_buffer->size_ = 0;
		tmp_encode_buffer->has_ = false;
		encode_vector_.push_back(tmp_encode_buffer);
	}
}

void fec_encoder::initMatrix (int n, int m) 
{
	matrix_ptr_.matrix_t_.m_row = m;
	matrix_ptr_.matrix_t_.m_col = n;
	for (int i = 0; i < m; ++i) 
	{
		for (int j = 0; j < n; ++j) 
		{
			matrix_ptr_.matrix_t_.m_data[i][j] = matrix_ptr_.galois8bit_ptr_.galoisPow(j + 1, i);
		}
	}
}

void fec_encoder::xorMulArr (unsigned char num, unsigned char *src, unsigned char *dest, int size) 
{
	if (!num) 
		return;

	for (int i = 0; i < size; ++i) 
	{
		(*dest++) ^= matrix_ptr_.galois8bit_ptr_.galoisMul(num, *src++);
	}
}

void fec_encoder::dealbuffer (unsigned char **c, unsigned char* buf, int buflen, int offset, int blen) 
{
	int cursize, pos = 0;
	unsigned char coef;

	int bnum = offset / blen;
	int boff = offset % blen;
	while (pos < buflen)
	{
		if (blen - boff > buflen - pos) 
			cursize = buflen - pos;
		else 
			cursize = blen - boff;

		for (int i = 0; i < encode_block_m_; ++i) 
		{
			coef = matrix_ptr_.matrix_t_.m_data[i][bnum];
			xorMulArr(coef, buf + pos, c[i] + boff, cursize);
		}

		pos += cursize;
		bnum ++;
		boff = 0;
	}
}

void fec_encoder::encode_data() 
{
	int blen, offset;
	unsigned char buffer[BUF_LEN] = {0};

	initMatrix(encode_block_n_, encode_block_m_);

	int size = get_encode_buffer_size();
	blen = GET_BLOCK_SIZE(size, encode_block_n_);

	unsigned char **c = (unsigned char**)malloc(sizeof(int*) * encode_block_m_);
	for (int i = 0; i < encode_block_m_; ++i) 
	{
		c[i] = (unsigned char*)malloc(sizeof(unsigned char) * blen);
		memset (c[i], 0, sizeof(unsigned char)*blen);
	}

	unsigned char* tmp_buffer = new unsigned char[size];
	memset(tmp_buffer, 0, size);

	std::lock_guard<std::recursive_mutex> encode_buffer_gurad(encode_buffer_lock_);
	int read_offset = 0;
	for (auto iter = encode_buffer_vector_.begin(); iter != encode_buffer_vector_.end(); iter++) 
	{
		encode_buffer *tmp_encode_buffer = *iter;
		if(tmp_encode_buffer->has_)
		{
			memcpy(tmp_buffer + read_offset, tmp_encode_buffer->buffer_, tmp_encode_buffer->size_);
			read_offset += tmp_encode_buffer->size_;
		}
	}

	offset = 0;
	do
	{
		//��ʼ��buffer;
		memset(buffer, 0, sizeof(unsigned char) * sizeof(buffer));

		int tmp_size = 0;
		if(offset + BUF_LEN > size)
			tmp_size = size - offset;
		else
			tmp_size = BUF_LEN;

		memcpy(buffer, tmp_buffer + offset, tmp_size);
		dealbuffer(c, buffer, tmp_size, offset, blen);

		offset += tmp_size;
	}while(offset < size);

	//��д�����������;
	std::lock_guard<std::recursive_mutex> encode_gurad(encode_lock_);
	for (int i = 0; i < encode_block_m_; i++) 
	{
		memcpy(encode_vector_[i]->buffer_, c[i], blen);
		encode_vector_[i]->size_ = blen;
		encode_vector_[i]->has_ = true;
	}

	//�ͷ�;
	for (int i = 0; i < encode_block_m_; ++i) 
	{
		free(c[i]);
	}
	free(c);
	delete[] tmp_buffer;
}

void fec_encoder::add_encode(unsigned char *buffer, int size, int postion)
{
	std::lock_guard<std::recursive_mutex> encode_buffer_gurad(encode_buffer_lock_);

	memset(encode_buffer_vector_[postion]->buffer_, 0, FRAMES_BODY_SIZE);
	memcpy(encode_buffer_vector_[postion]->buffer_, buffer, size);
	encode_buffer_vector_[postion]->size_ = size;
	encode_buffer_vector_[postion]->has_ = true;
	current_encode_buffer_size_++;
}

int fec_encoder::get_encode_buffer_size()
{
	std::lock_guard<std::recursive_mutex> encode_buffer_gurad(encode_buffer_lock_);

	int size = 0;
	for(auto iter = encode_buffer_vector_.begin(); iter != encode_buffer_vector_.end(); iter++)
	{
		encode_buffer *tmp_encode_buffer = *iter;
		if(tmp_encode_buffer->has_)
		{
			size += tmp_encode_buffer->size_;
		}
	}
	return size;
}

matrix_t fec_encoder::createMatrix (int *ib, int *ic, int lenb ,int lenc, int n) 
{
	int i, j;
	matrix_t A;
	A.m_row = lenb;
	A.m_col = lenb;
	for (j = 0; j < lenb; ++j) 
	{
		for (i = 0; i < lenb; ++i) 
		{
			A.m_data[j][i] = matrix_ptr_.matrix_t_.m_data[ic[j]-n][ib[i]];
		}
	}
	return A;
}
fec_decoder::fec_decoder()
{
	//init(FEC_GROUP_SIZE, FEC_REDUNDANCY_SIZE, FRAMES_BODY_SIZE);
}

fec_decoder::~fec_decoder(void)
{
	std::lock_guard<std::recursive_mutex> gurad(decode_lock_);

	for(size_t i = 0; i < decode_buffer_vector_.size(); i++)
	{
		decode_buffer *tmp_decode_buffer = decode_buffer_vector_[i];
		if(tmp_decode_buffer != nullptr)
		{
			delete tmp_decode_buffer;
			tmp_decode_buffer = nullptr;
		}
	}
	decode_buffer_vector_.clear();

	for(size_t i = 0; i < decode_vector_.size(); i++)
	{
		decode_buffer *tmp_decode_buffer = decode_vector_[i];
		if(tmp_decode_buffer != nullptr)
		{
			delete tmp_decode_buffer;
			tmp_decode_buffer = nullptr;
		}
	}
	decode_vector_.clear();
}

bool fec_decoder::decoder(std::shared_ptr<fec_group_buffer> group_buffer_ptr, std::vector<std::shared_ptr<frames_buffer>> &decoder_frames_vector)
{
	init_decode();
	uint64 min_index = 0, max_index = 0;
	ustd::rudp_public::get_group_min_max_index(group_buffer_ptr->group_id_, min_index, max_index);
	std::vector<uint64> tmp_decoder_vector;
	for(uint64 i = min_index; i <= max_index; i++)
	{
		std::map<uint64, std::shared_ptr<frames_buffer>>::iterator iter = group_buffer_ptr->frames_buffer_map_.find(i);
		if(iter != group_buffer_ptr->frames_buffer_map_.end())
		{
			std::shared_ptr<frames_buffer> tmp_frames_buffer = iter->second;
			add_buffer((unsigned char*)(&tmp_frames_buffer->body_), sizeof(tmp_frames_buffer->body_), i - min_index);
		}
		else
		{
			tmp_decoder_vector.push_back(i);
		}
	}

	for(auto iter = group_buffer_ptr->error_buffer_map_.begin(); iter != group_buffer_ptr->error_buffer_map_.end(); iter++)
	{
		std::shared_ptr<error_buffer> tmp_error_buffer = iter->second;
		add_decode((unsigned char*)(&tmp_error_buffer->data_), sizeof(tmp_error_buffer->data_), tmp_error_buffer->header_.group_index_);
	}
	decoder_frames_vector.clear();

	if(decode_data(group_buffer_ptr->group_size_))
	{
		for(uint64 i = 0; i < (uint64)tmp_decoder_vector.size(); i++)
		{
			int tmp_postion = tmp_decoder_vector[i] - min_index;
			std::shared_ptr<frames_buffer> frames_buffer_ptr(new frames_buffer);
			frames_buffer_ptr->header_.index_ = tmp_decoder_vector[i];
			frames_buffer_ptr->header_.message_type_ = message_data;
			memcpy(&frames_buffer_ptr->body_, decode_buffer_vector_[tmp_postion]->buffer_, decode_buffer_vector_[tmp_postion]->size_);
			decoder_frames_vector.push_back(frames_buffer_ptr);
		}
		return true;
	}
	return false;
}

void fec_decoder::init(int n, int m, int single_block_size)
{
	matrix_ptr_.galois8bit_ptr_.galoisEightBitInit();

	decode_block_n_ = n;
	decode_block_m_ = m;
	single_block_size_ = single_block_size;

	std::lock_guard<std::recursive_mutex> gurad(decode_lock_);
	decode_buffer_vector_.clear();
	decode_buffer_vector_.reserve(decode_block_n_);
	for(int i = 0; i < decode_block_n_; i++)
	{
		decode_buffer *tmp_decode_buffer = new decode_buffer;
		memset(tmp_decode_buffer, 0, sizeof(decode_buffer));
		tmp_decode_buffer->size_ = 0;
		tmp_decode_buffer->has_ = false;
		decode_buffer_vector_.push_back(tmp_decode_buffer);
	}

	decode_vector_.clear();
	decode_vector_.reserve(decode_block_m_);
	for(int i = 0; i < decode_block_m_; i++)
	{
		decode_buffer *tmp_decode_buffer = new decode_buffer;

		memset(tmp_decode_buffer, 0, sizeof(decode_buffer));
		tmp_decode_buffer->size_ = 0;
		tmp_decode_buffer->has_ = false;

		decode_vector_.push_back(tmp_decode_buffer);
	}

	// add_log(LOG_TYPE_INFO, "fec_decoder::init n=%d m=%d single_block_size=%d", n, m, single_block_size);
}

void fec_decoder::initMatrix (int n, int m) 
{
	matrix_ptr_.matrix_t_.m_row = m;
	matrix_ptr_.matrix_t_.m_col = n;
	for (int i = 0; i < m; ++i) 
	{
		for (int j = 0; j < n; ++j) 
		{
			matrix_ptr_.matrix_t_.m_data[i][j] = matrix_ptr_.galois8bit_ptr_.galoisPow(j + 1, i);
		}
	}
}

void fec_decoder::xorMulArr (unsigned char num, unsigned char *src, unsigned char *dest, int size) 
{
	if (!num) 
		return;

	for (int i = 0; i < size; ++i) 
	{
		(*dest++) ^= matrix_ptr_.galois8bit_ptr_.galoisMul(num, *src++);
	}
}

void fec_decoder::add_buffer(unsigned char *data, int size, int postion)
{
	std::lock_guard<std::recursive_mutex> gurad(decode_lock_);

	if(postion <= (int)decode_buffer_vector_.size() - 1)
	{
		decode_buffer_vector_[postion]->has_ = true;
		memcpy(decode_buffer_vector_[postion]->buffer_, data, size);
		decode_buffer_vector_[postion]->size_ = size;
	}
}

void fec_decoder::add_decode(unsigned char *data, int size, int postion)
{
	std::lock_guard<std::recursive_mutex> gurad(decode_lock_);

	if(postion <= (int)decode_vector_.size() - 1)
	{
		decode_vector_[postion]->has_ = true;
		memcpy(decode_vector_[postion]->buffer_, data, size);
		decode_vector_[postion]->size_ = size;
	}	
}

void fec_decoder::init_decode()
{
	std::lock_guard<std::recursive_mutex> gurad(decode_lock_);

	for(int i = 0; i < decode_block_n_; i++)
	{
		// decode_buffer *tmp_decode_buffer = decode_buffer_vector_[i];
		if(decode_buffer_vector_[i] != nullptr)
		{
			decode_buffer_vector_[i]->has_ = false;
			decode_buffer_vector_[i]->size_ = 0;
			memset(decode_buffer_vector_[i]->buffer_, 0, sizeof(decode_buffer_vector_[i]->buffer_));
		}
		
	}

	for(int i = 0; i < decode_block_m_; i++)
	{
		// decode_buffer *tmp_decode_buffer = decode_vector_[i];
		// memset(tmp_decode_buffer, 0, sizeof(decode_buffer));
		if(decode_vector_[i] != nullptr)
		{
			decode_vector_[i]->has_ = false;
			decode_vector_[i]->size_ = 0;
			memset(decode_vector_[i]->buffer_, 0, sizeof(decode_vector_[i]->buffer_));
		}
	}
}

void fec_decoder::fillerase (unsigned char **e, int begin, int end, int n, int m) 
{
	int i, j, pos, num, coef;
	unsigned char buf[BUF_LEN];
	for (i = begin; i < end; ++i) 
	{
		decode_buffer *tmp_decode_buffer = nullptr; 
		if(begin < decode_block_n_)
		{
			tmp_decode_buffer = decode_buffer_vector_[i];
		}
		else
		{
			tmp_decode_buffer = decode_vector_[i - begin];
		}

		if(!tmp_decode_buffer->has_)
		{
			continue;
		}

		pos = 0;
		while(pos < tmp_decode_buffer->size_)
		{
			if(pos + BUF_LEN > tmp_decode_buffer->size_)
				num = tmp_decode_buffer->size_ - pos;
			else
				num = BUF_LEN;

			memset(buf, 0, BUF_LEN);
			memcpy(buf, tmp_decode_buffer->buffer_ + pos, num);

			for (j = 0; j < m; ++j) 
			{
				if (i >= n && j != i-n) 
				{
					continue;
				}
				coef = (i >= n) ? 1 : matrix_ptr_.matrix_t_.m_data[j][i];
				xorMulArr(coef, buf, e[j] + pos, num);
			}
			pos += num;
		}
	}
}

matrix_t fec_decoder::createMatrix (int *ib, int *ic, int lenb ,int lenc) 
{
	int i, j;
	matrix_t A;
	A.m_row = lenb;
	A.m_col = lenb;
	for (j = 0; j < lenb; ++j) 
	{
		for (i = 0; i < lenb; ++i) 
		{
			A.m_data[j][i] = matrix_ptr_.matrix_t_.m_data[ic[j] - decode_block_n_][ib[i]];
		}
	}
	return A;
}

void fec_decoder::buildBlock(matrix_t *A, unsigned char **era, int *ic, int *ib, int lenb, int lenc, int blen) 
{
	int i, j, coef;
	unsigned char *buf = (unsigned char*)malloc(sizeof(unsigned char)*blen);
	for (i = 0; i < lenb; ++i) 
	{
		memset (buf, 0, sizeof(unsigned char) * blen);
		for (j = 0; j < lenc; ++j) 
		{
			coef = A->m_data[i][j];
			xorMulArr(coef, era[ic[j] - decode_block_n_], buf, blen);
		}
		decode_buffer_vector_[ib[i]]->has_ = true;
		decode_buffer_vector_[ib[i]]->size_ = blen;
		memcpy(decode_buffer_vector_[ib[i]]->buffer_, buf, blen);
	}
	free(buf);
}

void fec_decoder::get_loss_index_vector(int *ib, int *ic, int *lenb, int *lenc)
{
	int i;
	for (i = 0; i < decode_block_n_ + decode_block_m_; i++) 
	{
		if(i < decode_block_n_)
		{
			if(!decode_buffer_vector_[i]->has_)
			{
				ib[(*lenb)++] = i;
			}
		}
		else
		{
			if(decode_vector_[i - decode_block_n_]->has_)
			{
				ic[(*lenc)++] = i;
			}
		}
	}
}

bool fec_decoder::decode_data(int total_size) 
{
	int *ib = (int*)malloc(sizeof(int)*(decode_block_n_ + decode_block_m_));
	int *ic = (int*)malloc(sizeof(int)*(decode_block_n_ + decode_block_m_));

	int lenb = 0, lenc = 0;
	get_loss_index_vector(ib, ic, &lenb, &lenc);

	if (lenb > lenc) 
		return false;

	int blen = GET_BLOCK_SIZE(total_size, decode_block_n_);

	initMatrix(decode_block_n_, decode_block_m_);

	unsigned char **era;
	era = (unsigned char**)malloc(sizeof(unsigned char*) * decode_block_m_);
	for (int i = 0; i < decode_block_m_; ++i) 
	{
		era[i] = (unsigned char*)malloc(sizeof(unsigned char)*blen);
		memset (era[i], 0, sizeof(unsigned char) * blen);
	}
	fillerase(era, 0, decode_block_n_, decode_block_n_, decode_block_m_);
	fillerase(era, decode_block_n_, decode_block_n_ + decode_block_m_, decode_block_n_, decode_block_m_);

	matrix_t A, B;
	A = createMatrix(ib, ic, lenb, lenc);
	B = matrix_ptr_.matrixGauss(&A);

	buildBlock(&B, era, ic, ib, lenb, lenc, blen);

	for (int i = 0; i < decode_block_m_; ++i) 
	{
		free(era[i]);
	}
	free(era);
	free(ib);
	free(ic);

	return true;
}

void fec_decoder::add_log(const int log_type, const char *context, ...)
{
	const int array_length = 10 * KB;
	char log_text[array_length];
	memset(log_text, 0x00, array_length);

	va_list arg_ptr;
	va_start(arg_ptr, context);

	int result = harq_vsprintf(log_text, context, arg_ptr);

	va_end(arg_ptr);
	if (result <= 0)
		return;

	if (result > array_length)
		return;

	if (nullptr != write_log_ptr_)
	{
		write_log_ptr_->write_log3(log_type, log_text);
	}
}
