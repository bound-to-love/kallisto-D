#include "KmerIndex.h"
#include <seqan/sequence.h>
#include <seqan/index.h>
#include <seqan/seq_io.h>


using namespace seqan;

bool KmerIndex::loadSuffixArray(const ProgramOptions& opt ) {
  // open the fasta sequences

  index = TIndex(seqs);
  // try to open the file

  if (!open(getFibre(index, FibreSA()), (opt.index + ".sa").c_str(), OPEN_RDONLY | OPEN_QUIET)) {
    return false;
  }
  //setHaystack(finder, index); // finder searches index

  return true;
}

void KmerIndex::clearSuffixArray() {
  clear(index);
  //clear(finder);
}

void KmerIndex::BuildTranscripts(const ProgramOptions& opt) {
  const std::string& fasta = opt.transfasta;
  std::cerr << "[build] Loading fasta file " << fasta
            << std::endl;
  std::cerr << "[build] k: " << k << std::endl;


  SeqFileIn seqFileIn(fasta.c_str());

  // read fasta file
  seqan::StringSet<seqan::CharString> ids;
  readRecords(ids, seqs, seqFileIn);

  int transid = 0;


  if (!loadSuffixArray(opt)) {
    std::cerr << "[build] Constructing suffix array ... "; std::cerr.flush();

    index = TIndex(seqs);
    indexRequire(index, EsaSA());
    // write fasta to disk
    SeqFileOut fastaIndex;
    if (!open(fastaIndex, (opt.index+".fa").c_str())) {
      std::cerr << "Error: could not open file " << opt.index << ".fa for writing" << std::endl;
      exit(1);
    }
    try {
      writeRecords(fastaIndex, ids, seqs);
    } catch (IOError const& e) {
      std::cerr << "Error: writing to file " << opt.index << ". " << e.what() << std::endl;
    }
    close(fastaIndex);

    // write index to disk
    save(getFibre(index, FibreSA()),(opt.index + ".sa").c_str());

    std::cerr << " finished " << std::endl;
  } else {
    std::cerr << "[build] Found suffix array" << std::endl;
  }

  TFinder finder(index);
  find(finder, "A");
  clear(finder);


  assert(length(seqs) == length(ids));
  num_trans = length(seqs);


  std::cerr << "[build] Creating equivalence classes ... "; std::cerr.flush();

  // for each transcript, create it's own equivalence class
  for (int i = 0; i < length(seqs); i++ ) {
    std::string name(toCString(ids[i]));
    size_t p = name.find(' ');
    if (p != std::string::npos) {
      name = name.substr(0,p);
    }
    target_names_.push_back(name);

    CharString seq = value(seqs,i);
    trans_lens_.push_back(length(seq));

    std::vector<int> single(1,i);
    ecmap.insert({i,single});
    ecmapinv.insert({single,i});
  }

  // traverse the suffix tree
  int nk = 0;

  /*
  // for each transcript
  for (int i = 0; i < length(seqs); i++) {
    // if it it long enough
    CharString seq = value(seqs,i);
    if (length(seq) >= k) {
      KmerIterator kit(toCString(seq)), kit_end;

      // for each k-mer add to map
      for(; kit != kit_end; ++kit) {
        Kmer km = kit->first;
        Kmer rep = km.rep();
        if (kmap.find(km) != kmap.end()) {
          // we've seen this k-mer before
          continue;
        }

        nk++;
        // if (nk % 1000 == 0) {
        //   std::cerr << "."; std::cerr.flush();
        // }

        CharString kms(km.toString().c_str()); // hacky, but works

        std::vector<int> ecv;

        // find all instances of the k-mer
        clear(finder);
        while (find(finder, kms)) {
          ecv.push_back(getSeqNo(position(finder)));
        }
        // find all instances of twin
        reverseComplement(kms);
        clear(finder);
        while (find(finder,kms)) {
          ecv.push_back(getSeqNo(position(finder)));
        }

        // most common case
        if (ecv.size() == 1) {
          int ec = ecv[0];
          kmap.insert({rep, KmerEntry(ec)});
        } else {
          sort(ecv.begin(), ecv.end());
          std::vector<int> u;
          u.push_back(ecv[0]);
          for (int i = 1; i < ecv.size(); i++) {
            if (ecv[i-1] != ecv[i]) {
              u.push_back(ecv[i]);
            }
          }
          auto search = ecmapinv.find(u);
          if (search != ecmapinv.end()) {
            kmap.insert({rep, KmerEntry(search->second)});
          } else {
            int eqs_id = ecmapinv.size();
            ecmapinv.insert({u, eqs_id });
            ecmap.insert({eqs_id, u});
            kmap.insert({rep, KmerEntry(eqs_id)});
          }
        }
      }
    }
  }
  */



  Iterator<TIndex, TopDown<ParentLinks<>>>::Type it(index);
  do {

    if (repLength(it) >= k) {
      nk++;

      // if (nk % 1000 == 0) {
      //   std::cerr << "."; std::cerr.flush();
      // }

      //std::cout << representative(it) << std::endl;
      // process the k-mers
      CharString seq = representative(it);
      Kmer km(toCString(seq));
      Kmer rep = km.rep();
      if (kmap.find(rep) == kmap.end()) {
        // if we have never seen this k-mer before

        std::vector<int> ecv;

        for (auto x : getOccurrences(it)) {
          ecv.push_back(x.i1); // record transcript
        }

        // search for second part
        Kmer twin = km.twin(); // other k-mer
        clear(finder);
        while (find(finder, twin.toString().c_str())) {
          ecv.push_back(getSeqNo(position(finder)));
        }

        // common case
        if (ecv.size() == 1) {
          int ec = ecv[0];
          kmap.insert({rep, KmerEntry(ec)});
        } else {
          sort(ecv.begin(), ecv.end());
          std::vector<int> u;
          u.push_back(ecv[0]);
          for (int i = 1; i < ecv.size(); i++) {
            if (ecv[i-1] != ecv[i]) {
              u.push_back(ecv[i]);
            }
          }

          auto search = ecmapinv.find(u);
          if (search != ecmapinv.end()) {
            kmap.insert({rep, KmerEntry(search->second)});
          } else {
            int eqs_id = ecmapinv.size();
            ecmapinv.insert({u, eqs_id });
            ecmap.insert({eqs_id, u});
            kmap.insert({rep, KmerEntry(eqs_id)});
          }
        }
      }
    }
    // next step
    if (!goDown(it) || repLength(it) > k) {
      // if we can't go down or the sequence is too long
      do {
        if (!goRight(it)) {
          while (goUp(it) && !goRight(it)) {
            // go up the tree until you can go to the right
          }
        }
      } while (repLength(it) > k);
    }
  } while (!isRoot(it));


  std::cerr << " ... finished creating equivalence classes" << std::endl;


  // remove polyA close k-mers
  CharString polyA;
  resize(polyA, k, 'A');
  std::cerr << "[build] Removing all k-mers within hamming distance 1 of " << polyA << std::endl;

  {
    auto search = kmap.find(Kmer(toCString(polyA)).rep());
    if (search != kmap.end()) {
      kmap.erase(search);
    }
  }

  for (int i = 0; i < k; i++) {
    for (int a = 1; a < 4; a++) {
      CharString x(polyA);
      assignValue(x, i, Dna(a));
      {
        auto search = kmap.find(Kmer(toCString(x)).rep());
        if (search != kmap.end()) {
          kmap.erase(search);
        }
      }
      /*
      for (int j = i+1; j < k; j++) {
        CharString y(x);
        for (int b = 1; b < 4; b++) {
          assignValue(y, j, Dna(b));
          {
            auto search = kmap.find(Kmer(toCString(y)).rep());
            if (search != kmap.end()){
              kmap.erase(search);
            }
          }
        }
      }
      */
    }
  }

  int kset = 0;
  std::cerr << "[build] Computing skip-ahead ... "; std::cerr.flush();
  // find out how much we can skip ahead for each k-mer.
  for (auto& kv : kmap) {
    if (kv.second.fdist == -1 && kv.second.bdist == -1) {
      // ok we haven't processed the k-mer yet
      std::vector<Kmer> flist, blist;
      int ec = kv.second.id;

      // iterate in forward direction
      Kmer km = kv.first;
      Kmer end = km;
      Kmer last = end;
      Kmer twin = km.twin();
      bool selfLoop = false;
      flist.push_back(km);

      while (fwStep(end,end,ec)) {
        if (end == km) {
          // selfloop
          selfLoop = true;
          break;
        } else if (end == twin) {
          selfLoop = true;
          // mobius loop
          break;
        } else if (end == last.twin()) {
          // hairpin
          break;
        }
        flist.push_back(end);
        last = end;
      }

      Kmer front = twin;
      Kmer first = front;

      if (!selfLoop) {
        while (fwStep(front,front,ec)) {
          if (front == twin) {
            // selfloop
            selfLoop = true;
            break;
          } else if (front == km) {
            // mobius loop
            selfLoop = true;
            break;
          } else if (front == first.twin()) {
            // hairpin
            break;
          }
          blist.push_back(front);
          first = front;
        }
      }

      std::vector<Kmer> klist;
      for (auto it = blist.rbegin(); it != blist.rend(); ++it) {
        klist.push_back(it->twin());
      }
      for (auto x : flist) {
        klist.push_back(x);
      }

      int contigLength = klist.size();


      for (int i = 0; i < klist.size(); i++) {
        Kmer x = klist[i];
        Kmer xr = x.rep();
        bool forward = (x==xr);
        auto search = kmap.find(xr);

        if (forward) {
          search->second.fdist = contigLength-1-i;
          search->second.bdist = i;
        } else {
          search->second.fdist = i;
          search->second.bdist = contigLength-1-i;
        }
        kset++;
      }
    }
  }
  std::cerr << " finished computing skip-ahead, found for " << kset << " kmers"<< std::endl;

  std::cerr << "[build] Found " << num_trans << " transcripts"
            << std::endl;

  int eqs_id = num_trans;

  std::cerr << "[build] Created " << ecmap.size() << " equivalence classes from " << num_trans << " transcripts" << std::endl;

  std::cerr << "[build] K-mer map has " << kmap.size() << " k-mers" << std::endl;
}


void KmerIndex::write(const std::string& index_out, bool writeKmerTable) {
  std::ofstream out;
  out.open(index_out, std::ios::out | std::ios::binary);

  if (!out.is_open()) {
    // TODO: better handling
    std::cerr << "Error: index output file could not be opened!";
    exit(1);
  }

  // 1. write version
  out.write((char *)&INDEX_VERSION, sizeof(INDEX_VERSION));

  // 2. write k
  out.write((char *)&k, sizeof(k));

  // 3. write number of transcripts
  out.write((char *)&num_trans, sizeof(num_trans));

  // 4. write out transcript lengths
  for (int tlen : trans_lens_) {
    out.write((char *)&tlen, sizeof(tlen));
  }

  size_t kmap_size = kmap.size();

  if (writeKmerTable) {
    // 5. write number of k-mers in map
    out.write((char *)&kmap_size, sizeof(kmap_size));

    // 6. write kmer->ec values
    for (auto& kv : kmap) {
      out.write((char *)&kv.first, sizeof(kv.first));
      out.write((char *)&kv.second, sizeof(kv.second));
    }
  } else {
    // 5. write fake k-mer size
    kmap_size = 0;
    out.write((char *)&kmap_size, sizeof(kmap_size));

    // 6. write none of the kmer->ec values
  }
  // 7. write number of equivalence classes
  size_t tmp_size;
  tmp_size = ecmap.size();
  out.write((char *)&tmp_size, sizeof(tmp_size));

  // 8. write out each equiv class
  for (auto& kv : ecmap) {
    out.write((char *)&kv.first, sizeof(kv.first));

    // 8.1 write out the size of equiv class
    tmp_size = kv.second.size();
    out.write((char *)&tmp_size, sizeof(tmp_size));
    // 8.2 write each member
    for (auto& val: kv.second) {
      out.write((char *)&val, sizeof(val));
    }
  }

  // 9. Write out target ids
  // XXX: num_trans should equal to target_names_.size(), so don't need
  // to write out again.
  assert(num_trans == target_names_.size());
  for (auto& tid : target_names_) {
    // 9.1 write out how many bytes
    // XXX: Note: this doesn't actually encore the max targ id size.
    // might cause problems in the future
    tmp_size = tid.size();
    out.write((char *)&tmp_size, sizeof(tmp_size));

    // 9.2 write out the actual string
    out.write(tid.c_str(), tid.size());
  }

  out.flush();
  out.close();
}

bool KmerIndex::fwStep(Kmer km, Kmer& end, int ec) const {
  int j = -1;
  int fw_count = 0;
  for (int i = 0; i < 4; i++) {
    Kmer fw_rep = end.forwardBase(Dna(i)).rep();
    auto search = kmap.find(fw_rep);
    if (search != kmap.end()) {
      if (search->second.id != ec) {
        return false;
      }
      j = i;
      ++fw_count;
      if (fw_count > 1) {
        return false;
      }
    }
  }

  if (fw_count != 1) {
    return false;
  }

  Kmer fw = end.forwardBase(Dna(j));

  int bw_count = 0;
  for (int i = 0; i < 4; i++) {
    Kmer bw_rep = fw.backwardBase(Dna(i)).rep();
    if (kmap.find(bw_rep) != kmap.end()) {
      ++bw_count;
      if (bw_count > 1) {
        return false;
      }
    }
  }

  if (bw_count != 1) {
    return false;
  } else {
    if (fw != km) {
      end = fw;
      return true;
    } else {
      return false;
    }
  }

}

void KmerIndex::load(ProgramOptions& opt, bool loadKmerTable) {



  std::string& index_in = opt.index;
  std::ifstream in;

  // load sequences
  SeqFileIn seqFileIn;

  if (!open(seqFileIn, (index_in + ".fa").c_str())) {
    std::cerr << "Error: index fasta could not be opened " << std::endl;
    exit(1);
  }
  seqan::StringSet<seqan::CharString> ids; // not used
  readRecords(ids, seqs, seqFileIn);


  in.open(index_in, std::ios::in | std::ios::binary);

  if (!in.is_open()) {
    // TODO: better handling
    std::cerr << "Error: index input file could not be opened!";
    exit(1);
  }

  // 1. read version
  size_t header_version = 0;
  in.read((char *)&header_version, sizeof(header_version));

  if (header_version != INDEX_VERSION) {
    std::cerr << "Error: Incompatiple indices. Found version " << header_version << ", expected version " << INDEX_VERSION << std::endl
              << "Rerun with index to regenerate!";
    exit(1);
  }

  // 2. read k
  in.read((char *)&k, sizeof(k));
  if (Kmer::k == 0) {
    //std::cerr << "[index] no k has been set, setting k = " << k << std::endl;
    Kmer::set_k(k);
    opt.k = k;
  } else if (Kmer::k == k) {
    //std::cerr << "[index] Kmer::k has been set and matches" << k << std::endl;
    opt.k = k;
  } else {
    std::cerr << "Error: Kmer::k was already set to = " << Kmer::k << std::endl
              << "       conflicts with value of k  = " << k << std::endl;
    exit(1);
  }

  // 3. read number of transcripts
  in.read((char *)&num_trans, sizeof(num_trans));

  // 4. read length of transcripts
  trans_lens_.clear();
  trans_lens_.reserve(num_trans);

  for (int i = 0; i < num_trans; i++) {
    int tlen;
    in.read((char *)&tlen, sizeof(tlen));
    trans_lens_.push_back(tlen);
  }

  // 5. read number of k-mers
  size_t kmap_size;
  in.read((char *)&kmap_size, sizeof(kmap_size));

  std::cerr << "[index] k: " << k << std::endl;
  std::cerr << "[index] num_trans read: " << num_trans << std::endl;
  std::cerr << "[index] kmap size: " << kmap_size << std::endl;

  kmap.clear();
  if (loadKmerTable) {
    kmap.reserve(kmap_size);
  }

  // 6. read kmer->ec values
  Kmer tmp_kmer;
  KmerEntry tmp_val;
  for (size_t i = 0; i < kmap_size; ++i) {
    in.read((char *)&tmp_kmer, sizeof(tmp_kmer));
    in.read((char *)&tmp_val, sizeof(tmp_val));

    if (loadKmerTable) {
      kmap.insert({tmp_kmer, tmp_val});
    }
  }

  // 7. read number of equivalence classes
  size_t ecmap_size;
  in.read((char *)&ecmap_size, sizeof(ecmap_size));

  std::cerr << "[index] ecmap size: " << ecmap_size << std::endl;

  int tmp_id;
  int tmp_ecval;
  size_t vec_size;
  // 8. read each equiv class
  for (size_t i = 0; i < ecmap_size; ++i) {
    in.read((char *)&tmp_id, sizeof(tmp_id));

    // 8.1 read size of equiv class
    in.read((char *)&vec_size, sizeof(vec_size));

    // 8.2 read each member
    std::vector<int> tmp_vec;
    tmp_vec.reserve(vec_size);
    for (size_t j = 0; j < vec_size; ++j ) {
      in.read((char *)&tmp_ecval, sizeof(tmp_ecval));
      tmp_vec.push_back(tmp_ecval);
    }
    ecmap.insert({tmp_id, tmp_vec});
    ecmapinv.insert({tmp_vec, tmp_id});
  }

  // 9. read in target ids
  target_names_.clear();
  target_names_.reserve(num_trans);

  size_t tmp_size;
  char buffer[1024]; // if your target_name is longer than this, screw you.
  for (auto i = 0; i < num_trans; ++i) {
    // 9.1 read in the size
    in.read((char *)&tmp_size, sizeof(tmp_size));

    // 9.2 read in the character string
    in.read(buffer, tmp_size);

    std::string tmp_targ_id( buffer );
    target_names_.push_back(std::string( buffer ));

    // clear the buffer for next string
    memset(buffer,0,strlen(buffer));
  }

  in.close();
}

int KmerIndex::mapPair(const char *s1, int l1, const char *s2, int l2, int ec, TFinder& finder) const {
  bool d1 = true;
  bool d2 = true;
  int p1 = -1;
  int p2 = -1;


  KmerIterator kit1(s1), kit_end;

  bool found1 = false;
  while (kit1 != kit_end) {
    if (kmap.find(kit1->first.rep()) != kmap.end()) {
      found1 = true;
      break;
    }
  }

  if (!found1) {
    return -1;
  }

  clear(finder);
  //CharString c1(s1+kit1->second);
  // try to locate it in the suffix array
  //Prefix<CharString>::Type
  //CharString pre = prefix(c1,k);
  //setNeedle(finder, kit1->first.toString().c_str());
  while(find(finder, kit1->first.toString().c_str())) {
    if (getSeqNo(position(finder)) == ec) {
      p1 = getSeqOffset(position(finder)) - kit1->second;
      break;
    }
  }
  if (p1 < 0) {
    //reverseComplement(pre);
    clear(finder);
    while(find(finder, kit1->first.twin().toString().c_str())) {
      if (getSeqNo(position(finder)) == ec) {
        p1 = getSeqOffset(position(finder)) + k + kit1->second;
        d1 = false;
        break;
      }
    }
  }

  if (p1 == -1) {
    return -1;
  }


  KmerIterator kit2(s2);
  bool found2 = false;
  while (kit2 != kit_end) {
    if (kmap.find(kit2->first.rep()) != kmap.end()) {
      found2 = true;
      break;
    }
  }

  if (!found2) {
    return -1;
  }


  //CharString c2(s2+kit2->second);
  // try to locate it in the suffix array
  //Prefix<CharString>::Type pre = prefix(c2,k);
  clear(finder);
  while(find(finder, kit2->first.toString().c_str())) {
    if (getSeqNo(position(finder)) == ec) {
      p2 = getSeqOffset(position(finder)) - kit2->second;
      break;
    }
  }
  if (p2 < 0) {
    //reverseComplement(pre);
    clear(finder);
    while(find(finder, kit2->first.twin().toString().c_str())) {
      if (getSeqNo(position(finder)) == ec) {
        p2 = getSeqOffset(position(finder)) + k + kit2->second;
        d2 = false;
        break;
      }
    }
  }

  if (p2 == -1) {
    return -1;
  }

  if ((d1 && d2) || (!d1 && !d2)) {
    //std::cerr << "Reads map to same strand " << s1 << "\t" << s2 << std::endl;
    return -1;
  }

  if (p1>p2) {
    return p1-p2;
  } else {
    return p2-p1;
  }

}

// use:  match(s,l,v)
// pre:  v is initialized
// post: v contains all equiv classes for the k-mers in s
void KmerIndex::match(const char *s, int l, std::vector<std::pair<int, int>>& v) const {
  KmerIterator kit(s), kit_end;
  bool backOff = false;
  int nextPos = 0; // nextPosition to check
  bool jump = false;
  int lastEc = -1;
  for (int i = 0;  kit != kit_end; ++kit,++i) {
    if (kit->second >= nextPos) {
      // need to check it
      Kmer rep = kit->first.rep();
      auto search = kmap.find(rep);
      nextPos = kit->second+1; // need to look at the next position
      if (search != kmap.end()) {
        KmerEntry val = search->second;
        // is it inconsistent with the ec from the jump
        if (jump) {
          if (lastEc != val.id && lastEc != -1 && val.id != -1) {
            backOff = true;
            //std::cerr << "jump failed" << std::endl;
            break;
          } else {
            jump = false;
            lastEc = -1;
          }
        }

        v.push_back({val.id, kit->second});
        // see if we can skip ahead
        bool forward = (kit->first == rep);
        if (forward) {
          if (val.fdist > 0) {
            nextPos = kit->second + val.fdist;
            jump = true;
            lastEc = val.id;
          }
        } else {
          if (val.bdist > 0) {
            nextPos = kit->second + val.bdist;
            jump = true;
            lastEc = val.id;
          }
        }
      }
    }
    /*
    if (i==skip) {
      i=0;
    }
    if (i==0) {
      Kmer rep = kit->first.rep();
      auto search = kmap.find(rep);
      if (search != kmap.end()) {
        // if k-mer found
        v.push_back({search->second.id, kit->second}); // add equivalence class, and position
      }
    }
    */
  }


  if (backOff) {
    // backup plan, let's play it safe and search everythi
    v.clear();
    kit = KmerIterator(s);
    for (int i = 0; kit != kit_end; ++kit,++i) {
      if (i==skip) {
        i=0;
      }
      if (i==0) {
        // need to check it
        Kmer rep = kit->first.rep();
        auto search = kmap.find(rep);
        if (search != kmap.end()) {
          // if k-mer found
          v.push_back({search->second.id, kit->second}); // add equivalence class, and position
        }
      }
    }
  }
}

// use:  res = intersect(ec,v)
// pre:  ec is in ecmap, v is a vector of valid transcripts
//       v is sorted in increasing order
// post: res contains the intersection  of ecmap[ec] and v sorted increasing
//       res is empty if ec is not in ecma
std::vector<int> KmerIndex::intersect(int ec, const std::vector<int>& v) const {
  std::vector<int> res;
  auto search = ecmap.find(ec);
  if (search != ecmap.end()) {
    auto& u = search->second;
    res.reserve(v.size());

    auto a = u.begin();
    auto b = v.begin();

    while (a != u.end() && b != v.end()) {
      if (*a < *b) {
        ++a;
      } else if (*b < *a) {
        ++b;
      } else {
        // match
        res.push_back(*a);
        ++a;
        ++b;
      }
    }
  }
  return res;
}
