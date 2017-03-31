
#ifndef CRYPTO_OPENSSL_ENCRYPTOR_H_
#define CRYPTO_OPENSSL_ENCRYPTOR_H_

#include <string>

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <muduo/base/CurrentThread.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/StringPiece.h>
#include <muduo/base/Singleton.h>
#include <muduo/net/Buffer.h>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "utils/macros.h"

namespace crypto {

bool SupportedCipher(muduo::StringPiece cipher_name);

size_t GetCipherIvLength(muduo::StringPiece cipher_name);

namespace {

class OpenSSLInitSingleton : boost::noncopyable {
 public:
  static OpenSSLInitSingleton& GetInstance() {
    return muduo::Singleton<OpenSSLInitSingleton>::instance();
  }

 private:
  friend class muduo::Singleton<OpenSSLInitSingleton>;

  OpenSSLInitSingleton() {
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    const int kNumLocks = CRYPTO_num_locks();
    for (int i = 0; i < kNumLocks; ++i) {
      locks_.push_back(new muduo::MutexLock);
    }

    CRYPTO_set_locking_callback(LockingCallback);
    CRYPTO_THREADID_set_callback(CurrentThreadId);
  }

  ~OpenSSLInitSingleton() {
    CRYPTO_set_locking_callback(NULL);
    EVP_cleanup();
    ERR_free_strings();
  }

  static void CurrentThreadId(CRYPTO_THREADID* id) {
    CRYPTO_THREADID_set_numeric(
        id, static_cast<unsigned long>(muduo::CurrentThread::tid()));
  }

  static void LockingCallback(int mode, int n, const char* file, int line) {
    OpenSSLInitSingleton::GetInstance().OnLockingCallback(mode, n, file, line);
  }

  void OnLockingCallback(int mode, int n, const char* file, int line) {
    assert(static_cast<size_t>(n) < locks_.size());

    if (mode & CRYPTO_LOCK) {
      locks_[n].lock();
    } else {
      locks_[n].unlock();
    }
  }

 private:
  boost::ptr_vector<muduo::MutexLock> locks_;
};

void EnsureOpenSSLInit() {
  //(void)OpenSSLInitSingleton::GetInstance();
  ignore_result(OpenSSLInitSingleton::GetInstance());
}
}

class ScopedCipherCTX {
 public:
  explicit ScopedCipherCTX() { EVP_CIPHER_CTX_init(&ctx_); }
  ~ScopedCipherCTX() { EVP_CIPHER_CTX_cleanup(&ctx_); }
  EVP_CIPHER_CTX* get() { return &ctx_; }

 private:
  EVP_CIPHER_CTX ctx_;
};

typedef boost::shared_ptr<ScopedCipherCTX> ScopedCipherCTXPtr;

class Encryptor {
 public:
  Encryptor();

  virtual ~Encryptor();

  bool Init(muduo::StringPiece cipher_name,
            muduo::StringPiece passwd,
            muduo::StringPiece iv,
            bool do_encrypt);

  bool Update(muduo::StringPiece plaintext, std::string* ciphertext);

  bool Update(const char* plaintext, int text_size, std::string* ciphertext);

  bool Update(muduo::StringPiece plaintext,
              muduo::net::Buffer* ciphertext);

  bool Update(const muduo::net::Buffer* plaintext,
              muduo::net::Buffer* ciphertext);

  bool Update(const char* plaintext, int text_size,
              muduo::net::Buffer* ciphertext);

  const std::string& cipher_name() const { return cipher_name_; }

  const std::string& iv() const { return iv_; }

  const std::string& key() const { return key_; }

  const std::string& passwd() const { return passwd_; }

 private:
  bool Crypt(muduo::StringPiece input, std::string* output);

  bool Crypt(muduo::StringPiece input, muduo::net::Buffer* output);

  bool DoCrypt(muduo::StringPiece input, std::string* output);

 private:
  bool do_encrypt_;
  std::string cipher_name_;
  std::string key_;
  std::string iv_;
  std::string passwd_;
  ScopedCipherCTXPtr ctx_;
};

typedef boost::shared_ptr<Encryptor> EncryptorPtr;

}  // namespace crypto

#endif  // CRYPTO_OPENSSL_ENCRYPTOR_H_
