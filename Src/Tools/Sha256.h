/* Sha256.h -- SHA-256 Hash
2013-11-27 : Unknown : Public domain
2010-06-11 : Igor Pavlov : Public domain */

#pragma once

#include <nstd/Memory.h>

class Sha256
{
public:

  static const unsigned int blockSize = 64;

  Sha256() {reset();}

  void reset();

  void update(const unsigned char* data, unsigned int size);
  void finalize(unsigned char *digest);

  static void_t hash(const byte_t* data, size_t size, byte_t (&result)[blockSize])
  {
    Sha256 sha256;
    sha256.update(data, size);
    sha256.finalize((byte_t*)result);
  }

  static void_t hmac(const byte_t* key, size_t keySize, const byte_t* message, size_t messageSize, byte_t (&result)[blockSize])
  {
    Sha256 sha256;
    byte_t hashKey[blockSize];
    if(keySize > blockSize)
    {
      sha256.update(key, keySize);
      sha256.finalize(hashKey);
    }
    else
    {
      Memory::copy(hashKey, key, keySize);
      if(keySize < blockSize)
        Memory::zero(hashKey + keySize, blockSize - keySize);
    }
    
    byte_t oKeyPad[blockSize];
    byte_t iKeyPad[blockSize];
    for(int i = 0; i < 64; ++i)
    {
      oKeyPad[i] = hashKey[i] ^ 0x36;
      iKeyPad[i] = hashKey[i] ^ 0x5c;
    }
    byte_t hash[blockSize];
    sha256.update(iKeyPad, blockSize);
    sha256.update(message, messageSize);
    sha256.finalize(hash);
    sha256.update(oKeyPad, blockSize);
    sha256.update(hash, blockSize);
    sha256.finalize(result);
  }

public: // TODO: private
  unsigned int state[8];
  unsigned long long count;
  unsigned char buffer[64];
};
