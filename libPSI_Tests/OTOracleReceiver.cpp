#include "OTOracleReceiver.h"
#include "Common/Exceptions.h"
#include "Common/Log.h"
using namespace libPSI;


OTOracleReceiver::OTOracleReceiver(const block& seed)
	:mPrng(seed)
{
}

OTOracleReceiver::~OTOracleReceiver()
{
}




void OTOracleReceiver::Extend(
	const BitVector& choices,
	ArrayView<block> messages,
	PRNG& prng,
	Channel& chl)
{
	block test = mPrng.get_block(); 

	std::array<block, 2> ss;

	for (u64 doneIdx = 0; doneIdx < messages.size(); ++doneIdx)
	{
		ss[0] = mPrng.get_block();
		ss[1] = mPrng.get_block();

		messages[doneIdx] =  ss[choices[doneIdx]]; 

		//Log::out << " idx  " << doneIdx << "   " << messages[doneIdx] << "   " << (u32)choices[doneIdx] << Log::endl;
	}
	block test2;
	chl.recv((u8*)&test2, sizeof(block));
	if (neq(test, test2))
		throw std::runtime_error("");

}
