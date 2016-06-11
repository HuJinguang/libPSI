#include "AknOtSender.h"
#include "OT/Base/naor-pinkas.h"
#include "Common/Log.h"
#include "OT/LzKosOtExtSender.h"

namespace libPSI
{

	AknOtSender::AknOtSender()
	{
	}


	AknOtSender::~AknOtSender()
	{
	}


	void AknOtSender::init(u64 totalOTCount, u64 cutAndChooseThreshold, double p, OtExtSender & ots, std::vector<Channel*> & chls, PRNG & prng)
	{



		//if (ots.hasBaseOts() == false)
		//{
		//	PvwBaseOT baseOTs(chl, OTRole::Receiver);
		//	baseOTs.exec_base(prng);
		//	ots.setBaseOts(baseOTs.receiver_outputs, baseOTs.receiver_inputs);
		//}


		////std::thread extThread([&]() {
		//ots.Extend(otMessages, prng, chl);
		////});



		auto& chl0 = *chls[0];

		if (ots.hasBaseOts() == false)
		{
			//Timer timer;
			//timer.setTimePoint("base start");
			//PvwBaseOT base(chl0, OTRole::Receiver);
			//base.exec_base(prng);
			//std::array<std::array<block, 2>, BASE_OT_COUNT> baseMsg;
			std::array<block, BASE_OT_COUNT> baseMsg;
			BitVector choices(BASE_OT_COUNT);
			choices.randomize(prng);

			//crypto crpto(128, prng.get_block());
			NaorPinkas base;
			base.Receiver(baseMsg, choices, chl0, prng, 2);

			ots.setBaseOts(baseMsg, choices);

			//timer.setTimePoint("baseDone");
			//Log::out << timer;
		}

		mMessages.resize(totalOTCount);
		mSampled.resize(totalOTCount);

		auto cncRootSeed = prng.get_block();
		PRNG gg(cncRootSeed);
		std::vector<PRNG> cncGens(chls.size()); ;
		for (auto& b : cncGens)
			b.SetSeed(gg.get_block());

		//otMessages.resize(totalOTCount);

		std::atomic<u32> extRemaining((u32)chls.size());
		std::promise<void> extDoneProm;
		std::shared_future<void> extDoneFuture(extDoneProm.get_future());

		std::vector<std::unique_ptr<OtExtSender>> parOts(chls.size()-1);
		std::vector<std::thread> parThrds(chls.size()-1);

		u32 px = (u32)(u32(-1) * p);

		std::mutex finalMtx;
		u64 totalOnesCount(0);
		block totalSum(ZeroBlock);

		auto routine = [&](u64 t, block extSeed, OtExtSender& otExt, Channel& chl)
		{
			// round up to the next 128 to make sure we aren't wasting OTs in the extension...
			u64 start = std::min(roundUpTo(t *     mMessages.size() / chls.size(), 128), mMessages.size());
			u64 end = std::min(roundUpTo((t + 1) * mMessages.size() / chls.size(), 128), mMessages.size());

			ArrayView<std::array<block, 2>> range(
				mMessages.begin() + start,
				mMessages.begin() + end);

			PRNG prng(extSeed);
			//Log::out << Log::lock << "send 0 " << end << Log::endl;
			u8 shaBuff[SHA1::HashSize];


			otExt.Extend(range, prng, chl);


			//Log::out << Log::unlock;

			if (--extRemaining)
				extDoneFuture.get();
			else
				extDoneProm.set_value();

			if (t == 0)
			{
				gTimer.setTimePoint("AknOt.SenderExtDone");
				chl.asyncSend(&cncRootSeed, sizeof(block));
			}

			u64 sampleCount(0);

			auto sampleIter = mSampled.begin() + start;
			block partialSum(ZeroBlock);
			u64 onesCount(0);



			ByteStream choiceBuff;
			chl.recv(choiceBuff);
			auto choiceIter = choiceBuff.bitIterBegin();
			u64 bitsRemaining = choiceBuff.size() * 8;


			//Log::out << Log::lock << "send " << end << "  " << px << Log::endl;
			for (u64 i = start, j = 0; i < end; ++i)
			{
				auto vv = cncGens[t].get_u32();
				u8 c = (vv <= px);
				*sampleIter = c;
				++sampleIter;



				if (c)
				{
					if (bitsRemaining-- == 0)
					{
						chl.recv(choiceBuff);
						bitsRemaining = choiceBuff.size() * 8 - 1;
						choiceIter = choiceBuff.bitIterBegin();
						//Log::out << "   " << i << Log::endl;

					}

					++sampleCount;

					u8 cc = *choiceIter;
					//Log::out << (u32)cc;

					if (cc == 0 && dynamic_cast<LzKosOtExtSender*>(&ots))
					{
						// if this is a zero message and our OT extension class is
						// LzKosOtExtSender, then we need to hash the 0-message now
						// because it was lazy and didn't ;)

						SHA1 sha;
						sha.Update(mMessages[i][0]);
						sha.Final(shaBuff);
						mMessages[i][0] = *(block*)shaBuff;
					}

					partialSum = partialSum ^ mMessages[i][cc];
					//Log::out << mMessages[i][cc] << " " << partialSum << " " << "  " << i << "  " << (u32)cc << "  " << vv << Log::endl;



					onesCount += cc;

					++choiceIter;
				}  
			}

			//Log::out << Log::endl << Log::unlock;

			std::lock_guard<std::mutex>lock(finalMtx);
			totalOnesCount += onesCount;
			totalSum = totalSum ^ partialSum;
		};

		for (u64 i = 0; i < parOts.size(); ++i)
		{
			parOts[i] = std::move(ots.split());
			auto seed = prng.get_block();
			parThrds[i] = std::thread([&,seed, i]()
			{
				auto  t = i + 1;

				routine(t,seed, *parOts[i], *chls[t]);
			});
		}

		routine(0, prng.get_block(), ots, chl0);



		for (auto& thrd : parThrds)
			thrd.join();


		block proof;
		chl0.recv(&proof, sizeof(block));
		if (totalOnesCount > cutAndChooseThreshold ||
			neq(proof, totalSum))
		{
			Log::out << "cnc failed. total ones Count = " << totalOnesCount << "  and threshold "<< cutAndChooseThreshold  << Log::endl
				<< "my computed block  = " << totalSum << "  vs " << proof  <<Log::endl;
			throw std::runtime_error("failed cut and choose");
		}
	}
}
