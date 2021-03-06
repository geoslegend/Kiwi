#include "stdafx.h"
#include "Utils.h"
#include "Kiwi.h"
#include "KFeatureTestor.h"
#include "KModelMgr.h"


KPOSTag Kiwi::identifySpecialChr(k_wchar chr)
{
	switch (chr)
	{
	case ' ':
	case '\t':
	case '\r':
	case '\n':
	case '\v':
	case '\f':
		return KPOSTag::UNKNOWN;
	}
	if (iswdigit(chr)) return KPOSTag::SN;
	if (('A' <= chr && chr <= 'Z') ||
		('a' <= chr && chr <= 'z'))  return KPOSTag::SL;
	if (0xac00 <= chr && chr < 0xd7a4) return KPOSTag::MAX;
	switch (chr)
	{
	case '.':
	case '!':
	case '?':
		return KPOSTag::SF;
	case '-':
	case '~':
	case 0x223c:
		return KPOSTag::SO;
	case 0x2026:
		return KPOSTag::SE;
	case ',':
	case ';':
	case ':':
	case '/':
	case 0xb7:
		return KPOSTag::SP;
	case '"':
	case '\'':
	case '(':
	case ')':
	case '<':
	case '>':
	case '[':
	case ']':
	case '{':
	case '}':
	case 0xad:
	case 0x2015:
	case 0x2018:
	case 0x2019:
	case 0x201c:
	case 0x201d:
	case 0x226a:
	case 0x226b:
	case 0x2500:
	case 0x3008:
	case 0x3009:
	case 0x300a:
	case 0x300b:
	case 0x300c:
	case 0x300d:
	case 0x300e:
	case 0x300f:
	case 0x3010:
	case 0x3011:
	case 0x3014:
	case 0x3015:
	case 0xff0d:
		return KPOSTag::SS;
	}
	if ((0x2e80 <= chr && chr <= 0x2e99) ||
		(0x2e9b <= chr && chr <= 0x2ef3) ||
		(0x2f00 <= chr && chr <= 0x2fd5) ||
		(0x3005 <= chr && chr <= 0x3007) ||
		(0x3021 <= chr && chr <= 0x3029) ||
		(0x3038 <= chr && chr <= 0x303b) ||
		(0x3400 <= chr && chr <= 0x4db5) ||
		(0x4e00 <= chr && chr <= 0x9fcc) ||
		(0xf900 <= chr && chr <= 0xfa6d) ||
		(0xfa70 <= chr && chr <= 0xfad9)) return KPOSTag::SH;
	if (0xd800 <= chr && chr <= 0xdfff) return KPOSTag::SH;
	return KPOSTag::SW;
}

vector<vector<KWordPair>> Kiwi::splitPart(const k_wstring & str)
{
	vector<vector<KWordPair>> ret;
	ret.emplace_back();
	for (auto c : str)
	{
		auto tag = identifySpecialChr(c);
		if (tag == KPOSTag::UNKNOWN)
		{
			if(!ret.back().empty()) ret.emplace_back();
			continue;
		}
		if (ret.back().empty())
		{
			ret.back().emplace_back(k_wstring{ &c, 1 }, tag);
			continue;
		}
		if ((ret.back().back().second == KPOSTag::SN && c == '.') ||
			ret.back().back().second == tag)
		{
			ret.back().back().first.push_back(c);
			continue;
		}
		if (ret.back().back().second == KPOSTag::SF) ret.emplace_back();
		ret.back().emplace_back(k_wstring{ &c , 1 }, tag);
	}
	return ret;
}

Kiwi::Kiwi(const char * modelPath, size_t _maxCache) : maxCache(_maxCache)
{
	mdl = make_shared<KModelMgr>(modelPath);
}

int Kiwi::addUserWord(const k_wstring & str, KPOSTag tag)
{
	if (!verifyHangul(str)) return -1;
	mdl->addUserWord(splitJamo(str), tag);
	return 0;
}

int Kiwi::addUserRule(const k_wstring & str, const vector<pair<k_wstring, KPOSTag>>& morph)
{
	if (!verifyHangul(str)) return -1;
	vector<pair<k_string, KPOSTag>> jmMorph;
	jmMorph.reserve(morph.size());
	for (auto& m : morph)
	{
		if (!verifyHangul(m.first)) return -1;
		jmMorph.emplace_back(splitJamo(m.first), m.second);
	}
	mdl->addUserRule(splitJamo(str), jmMorph);
	return 0;
}

int Kiwi::loadUserDictionary(const char * userDictPath)
{
	FILE* file = nullptr;
	if (fopen_s(&file, userDictPath, "r")) return -1;
	char buf[4096];
	wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
	while (fgets(buf, 4096, file))
	{
		if (buf[0] == '#') continue;
		auto wstr = converter.from_bytes(buf);
		auto chunks = split(wstr, '\t');
		if (chunks.size() < 2) continue;
		if (!chunks[1].empty()) 
		{
			auto pos = makePOSTag(chunks[1]);
			if (pos != KPOSTag::MAX)
			{
				addUserWord(chunks[0], pos);
				continue;
			}
		}
		
		vector<pair<k_wstring, KPOSTag>> morphs;
		for (size_t i = 1; i < chunks.size(); i++) 
		{
			auto cc = split(chunks[i], '/');
			if (cc.size() != 2) goto loopContinue;
			auto pos = makePOSTag(cc[1]);
			if (pos == KPOSTag::MAX) goto loopContinue;
			morphs.emplace_back(cc[0], pos);
		}
		addUserRule(chunks[0], morphs);
	loopContinue:;
	}
	fclose(file);
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	kt = mdl->getTrie();
	return 0;
}

k_vchar getBacks(const k_vpcf& cands)
{
	char buf[128] = { 0 };
	k_vchar ret;
	for (auto c : cands)
	{
		auto back = c.first.back();
		assert(back >= 0);
		if (!buf[back])
		{
			ret.emplace_back(back);
			buf[back] = 1;
		}
	}
	return ret;
}

KResult Kiwi::analyze(const k_wstring & str) const
{
	return analyze(str, MIN_CANDIDATE)[0];
}

vector<KResult> Kiwi::analyze(const k_wstring & str, size_t topN) const
{
	vector<KInterResult> cands(1);
	auto parts = splitPart(str);
	auto sortFunc = [](const auto& x, const auto& y)
	{
		return x.second > y.second;
	};
	for (auto p : parts)
	{
		for (size_t cid = 0; cid < p.size(); cid++)
		{
			auto& c = p[cid];
			if (c.second != KPOSTag::MAX)
			{
				for (auto& cand : cands)
				{
					cand.first.emplace_back(nullptr, c.first, c.second);
				}
				continue;
			}
			auto jm = splitJamo(c.first);
			auto ar = analyzeJM(jm, topN, cid ? p[cid - 1].second : KPOSTag::UNKNOWN, cid + 1 < p.size() ? p[cid + 1].second : KPOSTag::UNKNOWN);
			vector<tuple<short, short, float>> probs;
			probs.reserve(cands.size() * ar.size());
			for (size_t i = 0; i < cands.size(); i++) for (size_t j = 0; j < ar.size(); j++)
			{
				probs.emplace_back((short)i, (short)j, cands[i].second + ar[j].second);
			}
			sort(probs.begin(), probs.end(), [](const auto& x, const auto& y)
			{
				return get<float>(x) > get<float>(y);
			});
			vector<KInterResult> newCands;
			newCands.reserve(min(topN, probs.size()));
			for_each(probs.begin(), probs.begin() + min(max(topN, (size_t)MIN_CANDIDATE), probs.size()), [&newCands, &cands, &ar](auto p)
			{
				const KInterResult& arp = ar[get<1>(p)];
				newCands.emplace_back(cands[get<0>(p)]);
				newCands.back().second += arp.second;
				auto& ms = newCands.back().first;
				size_t inserted = ms.size();
				ms.insert(ms.end(), arp.first.begin(), arp.first.end());
#ifdef USE_DIST_MAP
				for (size_t i = inserted; i < ms.size(); i++)
				{
					auto& mi = get<0>(ms[i]);
					if (!mi) continue;
					if (!mi->distMap) continue;
					for (size_t j = inserted < 5 ? 0 : inserted - 5; j < i; j++)
					{
						auto& mj = get<0>(ms[j]);
						if (!mj) continue;
						newCands.back().second += mi->getDistMap(mj);
					}
				}
#endif
			});
			swap(cands, newCands);
		}
	}
	vector<KResult> ret;
	ret.reserve(cands.size());
	for (const auto& c : cands)
	{
		if (ret.size() >= topN) break;
		ret.emplace_back(vector<KWordPair>{}, c.second);
		ret.back().first.reserve(c.first.size());
		for (const auto& m : c.first)
		{
			auto morpheme = get<0>(m);
			if (morpheme) ret.back().first.emplace_back(*morpheme->wform, morpheme->tag);
			else ret.back().first.emplace_back(get<1>(m), get<2>(m));
		}
	}
	return ret;
}

void Kiwi::clearCache()
{
	tempCache = {};
	freqCache = {};
	cachePriority = {};
}

vector<const KChunk*> Kiwi::divideChunk(const k_vchunk& ch)
{
	vector<const KChunk*> ret;
	const KChunk* s = &ch[0];
	size_t n = 1;
	for (auto& c : ch)
	{
		if (n >= DIVIDE_BOUND && &c > s + 1)
		{
			ret.emplace_back(s);
			s = &c;
			n = 1;
		}
		if (c.isStr()) continue;
		n *= c.form->candidate.size();
	}
	if (s != &ch[0] + ch.size())
	{
		ret.emplace_back(s);
	}
	return ret;
}

vector<k_vpcf> Kiwi::calcProbabilities(const KChunk * pre, const KChunk * ch, const KChunk * end, const char* ostr, size_t len) const
{
	static bool(*vowelFunc[])(const char*, const char*) = {
		KFeatureTestor::isPostposition,
		KFeatureTestor::isVowel,
		KFeatureTestor::isVocalic,
		KFeatureTestor::isVocalicH,
		KFeatureTestor::notVowel,
		KFeatureTestor::notVocalic,
		KFeatureTestor::notVocalicH,
	};
	static bool(*polarFunc[])(const char*, const char*) = {
		KFeatureTestor::isPositive,
		KFeatureTestor::notPositive
	};

	vector<k_vpcf> ret(pre->getCandSize());
	array<char, 256> idx = { 0, };
	size_t idxSize = end - ch + 1;
#define IDX_SIZE idxSize
	//vector<char> idx(end - ch + 1);
	float minThreshold = P_MIN;
	while (1)
	{
		float ps = 0;
		KMorpheme tmpMorpheme;
		const KMorpheme* beforeMorpheme = pre->getMorpheme(idx[0], &tmpMorpheme);
		bool beforeFormNull = true;
		for (size_t i = 1; i < IDX_SIZE; i++)
		{
			auto curMorpheme = ch[i - 1].getMorpheme(idx[i], &tmpMorpheme);
			if (!curMorpheme || (KPOSTag::SF <= curMorpheme->tag && curMorpheme->tag <= KPOSTag::SN))
			{
				ps += mdl->getTransitionP(beforeMorpheme, curMorpheme);
				beforeFormNull = true;
				beforeMorpheme = curMorpheme;
				continue;
			}
			// if this is unknown morpheme
			//if (curMorpheme->chunks.empty() && !curMorpheme->getForm().empty() && curMorpheme->tag == KPOSTag::UNKNOWN)
			if (!curMorpheme->kform)
			{
				tmpMorpheme.tag = mdl->findMaxiumTag(beforeMorpheme, i + 1 < IDX_SIZE ? ch[i].getMorpheme(idx[i + 1], nullptr) : nullptr);
			}

			if (beforeMorpheme->combineSocket && beforeMorpheme->tag != KPOSTag::UNKNOWN && beforeMorpheme->combineSocket != curMorpheme->combineSocket)
			{
				goto next;
			}
			if (curMorpheme->combineSocket && curMorpheme->tag == KPOSTag::UNKNOWN && beforeMorpheme->combineSocket != curMorpheme->combineSocket)
			{
				goto next;
			}

			if (!beforeFormNull)
			{
				const char* bBegin;
				const char* bEnd;
				if (beforeMorpheme->kform)
				{
					bBegin = &(*beforeMorpheme->kform)[0];
					bEnd = bBegin + beforeMorpheme->kform->size();
				}
				else
				{
					bBegin = ostr + ch[i - 2].begin;
					bEnd = ostr + ch[i - 2].end;
				}
				if ((int)curMorpheme->vowel && !vowelFunc[(int)curMorpheme->vowel - 1](bBegin, bEnd)) goto next;
				if ((int)curMorpheme->polar && !polarFunc[(int)curMorpheme->polar - 1](bBegin, bEnd)) goto next;
			}
			ps += mdl->getTransitionP(beforeMorpheme, curMorpheme);
			ps += curMorpheme->p;
			beforeFormNull = false;
			beforeMorpheme = curMorpheme;
			if (ps <= minThreshold) goto next;
		}
		ret[idx[0]].emplace_back(k_vchar{ idx.begin() + 1, idx.begin() + IDX_SIZE }, ps);
	next:;

		idx[0]++;
		for (size_t i = 0; i < IDX_SIZE; i++)
		{
			if (i ? (idx[i] >= ch[i - 1].getCandSize()) : (idx[i] >= pre->getCandSize()))
			{
				idx[i] = 0;
				if (i + 1 >= IDX_SIZE) goto exit;
				idx[i + 1]++;
			}
		}
	}
exit:;
	return ret;
}

vector<KInterResult> Kiwi::analyzeJM(const k_string & jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const
{
	string cfind{ jm.begin(), jm.end() };
	cfind.push_back((char)prefix + 64);
	cfind.push_back((char)suffix + 64);
	auto cached = findCache(cfind);
	if (cached) return *cached;

	auto sortFunc = [](const auto& x, const auto& y)
	{
		return x.second > y.second;
	};
	topN = max((size_t)MIN_CANDIDATE, topN);
	vector<pair<vector<tuple<const KMorpheme*, k_string, KPOSTag>>, float>> cands;
	KMorpheme preMorpheme{ "", prefix }, sufMorpheme{ "", suffix };
	KForm preForm, sufForm;
	preForm.candidate.emplace_back(&preMorpheme);
	sufForm.candidate.emplace_back(&sufMorpheme);
	KChunk preTemp{ &preForm }, sufTemp{ &sufForm };

	auto chunks = kt->split(jm, prefix != KPOSTag::UNKNOWN);
	for (auto& s : chunks)
	{
#ifdef _DEBUG
		for (auto& t : s)
		{
			printJM(t, &jm[0]);
			printf(", ");
		}
		printf("\n");
#endif

		if (suffix != KPOSTag::UNKNOWN)
		{
			s.push_back(sufTemp);
		}
		else
		{
			s.emplace_back(0, 0);
		}
		auto chunks = divideChunk(s);
		chunks.emplace_back(&s[0] + s.size());
		const KChunk* pre = &preTemp;
		k_vpcf pbFinal;

		for (size_t i = 1; i < chunks.size(); i++)
		{
			auto pb = calcProbabilities(pre, chunks[i - 1], chunks[i], &jm[0], jm.size());
			if (i == 1)
			{
				sort(pb[0].begin(), pb[0].end(), sortFunc);
				pbFinal.insert(pbFinal.end(), pb[0].begin(), pb[0].begin() + min(topN, pb[0].size()));
			}
			else
			{
				for (auto b : getBacks(pbFinal))
				{
					sort(pb[b].begin(), pb[b].end(), sortFunc);
				}
				k_vpcf tmpFinal;
				for (auto cand : pbFinal)
				{
					auto& pbBack = pb[cand.first.back()];
					for (size_t n = 0; n < topN && n < pbBack.size(); n++)
					{
						auto newP = cand.second + pbBack[n].second;
						if (newP <= P_MIN) continue;
						tmpFinal.emplace_back(cand.first, newP);
						tmpFinal.back().first.insert(tmpFinal.back().first.end(), pbBack[n].first.begin(), pbBack[n].first.end());
					}
				}
				sort(tmpFinal.begin(), tmpFinal.end(), sortFunc);
				pbFinal = { tmpFinal.begin(), tmpFinal.begin() + min(topN, tmpFinal.size()) };
			}
			pre = chunks[i] - 1;
		}
		for (auto pb : pbFinal)
		{
			cands.emplace_back();
			cands.back().second = pb.second;
			size_t n = 0;
			pb.first.pop_back();
			for (auto pbc : pb.first)
			{
				//if (!morpheme) continue;
				if (s[n].isStr())
				{
					const KMorpheme* before = nullptr;
					const KMorpheme* next = nullptr;
					if (n && !s[n - 1].isStr())
					{
						before = s[n - 1].form->candidate[pb.first[n - 1]];
					}
					if (n + 1 < pb.first.size() && !s[n + 1].isStr())
					{
						next = s[n + 1].form->candidate[pb.first[n + 1]];
					}
					auto predictTag = mdl->findMaxiumTag(before, next);
					cands.back().first.emplace_back(nullptr, k_string{ &jm[0] + s[n].begin, &jm[0] + s[n].end }, predictTag);
				}
				else
				{
					auto morpheme = s[n].form->candidate[pbc];
					// If is simple morpheme
					if (!morpheme->chunks)
					{
						cands.back().first.emplace_back(morpheme, k_string{}, KPOSTag{});
					}
					// complex morpheme
					else for (auto pbcc : *morpheme->chunks)
					{
						// if post morpheme must be combined
						if (pbcc->tag == KPOSTag::V)
						{
							//cands.back().first.back().first += pbcc->getForm();
							//assert(!get<0>(cands.back().first.back())->chunks.empty());
							//get<0>(cands.back().first.back()) = get<0>(cands.back().first.back())->chunks[0];
							auto& bm = get<0>(cands.back().first.back());
							assert(bm->getCombined());
							bm = bm->getCombined();
						}
						else cands.back().first.emplace_back(pbcc, k_string{}, KPOSTag{});
					}
				}
				n++;
			}
		}
	}
	
	vector<KInterResult> ret;
	ret.reserve(cands.size());
	if (cands.empty())
	{
		ret.emplace_back();
		ret.back().first.emplace_back(nullptr, joinJamo(jm), KPOSTag::UNKNOWN);
		ret.back().second = P_MIN;
	}
	else
	{
		sort(cands.begin(), cands.end(), sortFunc);
		transform(cands.begin(), cands.begin() + min(topN, cands.size()), back_inserter(ret), [](const auto& e)
		{
			KInterResult kr;
			for (auto& i : e.first)
			{
				if(get<0>(i)) kr.first.emplace_back(get<0>(i) , k_wstring{}, KPOSTag{});
				else  kr.first.emplace_back(nullptr, joinJamo(get<1>(i)), get<2>(i));
			}
			kr.second = e.second;
			return kr;
		});
	}
	addCache(cfind, ret);
	return ret;
}

bool Kiwi::addCache(const string & jm, const vector<KInterResult>& value) const
{
	if (maxCache == (size_t)-1) return freqCache.emplace(jm, value).second;
	if (tempCache.size() >= maxCache) tempCache.clear();
	++cachePriority[jm];
	return tempCache.emplace(jm, value).second;
}

vector<KInterResult>* Kiwi::findCache(const string & jm) const
{
	auto cit = freqCache.find(jm);
	if (cit == freqCache.end())
	{
		cit = tempCache.find(jm);
		++cachePriority[jm];
		if (cit == tempCache.end()) return nullptr;
		if (freqCache.size() < maxCache)
		{
			auto ret = &freqCache.emplace(*cit).first->second;
			return ret;
		}
		return &cit->second;
	}
	return &cit->second;
}
