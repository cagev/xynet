//
// Created by xuanyi on 4/28/20.
//

#ifndef XYNET_SOCKET_READ_H
#define XYNET_SOCKET_READ_H

#include <type_traits>
#include "xynet/buffer.h"
#include "xynet/socket/detail/async_operation.h"
#include "xynet/detail/file_descriptor_traits.h"

namespace xynet
{

template<detail::FileDescriptorPolicy P, typename F>
struct operation_recv_all
{
  template<typename BufferSequence>
  struct async_recvmsg : public async_operation<P, async_recvmsg<BufferSequence>>
  {
    template<typename... Args>
    async_recvmsg(F& socket, Args&&... args) noexcept
      :async_operation<P, async_recvmsg<BufferSequence>>{&async_recvmsg::on_recv_completed}
      ,m_socket{socket}
      ,m_buffers{static_cast<Args&&>(args)...}
      ,m_bytes_transferred{}
      ,m_msghdr{}
    {
      std::tie(m_msghdr.msg_iov,
               m_msghdr.msg_iovlen) = m_buffers.get_iov_span();
    }

    static void on_recv_completed(async_operation_base *base) noexcept
    {
      auto *op = static_cast<async_recvmsg<BufferSequence>*>(base);
      op->update_result();
    }

    void try_start()
    noexcept (detail::FileDescriptorPolicyUseErrorCode<P>)
    {
      auto recvmsg = [this](::io_uring_sqe* sqe)
      {
        ::io_uring_prep_recvmsg(sqe,
                                m_socket.get(),
                                &m_msghdr,
                                0);

        sqe->user_data = reinterpret_cast<uintptr_t>(this);
      };
      async_operation_base::get_service()->try_submit_io(recvmsg);
    }

    void update_result()
    {
      if
      (
      !  async_operation_base::no_error_in_result()
      || async_operation_base::get_res() == 0
      )
      {
        async_operation_base::get_awaiting_coroutine().resume();
      }
      else
      {
        m_bytes_transferred += async_operation_base::get_res();
        m_buffers.commit(async_operation_base::get_res());
        std::tie(m_msghdr.msg_iov,
                 m_msghdr.msg_iovlen) = m_buffers.get_iov_span();
        if(m_msghdr.msg_iov == nullptr)
        {
          async_operation_base::get_awaiting_coroutine().resume();
        }
        else
        {
          try_start();
        }
      }
    }

    auto get_result()
    noexcept (detail::FileDescriptorPolicyUseErrorCode<P>)
    ->detail::file_descriptor_operation_return_type_t<P, int>
    {
      if(async_operation_base::get_res() == 0)
      {
        return async_throw_or_return<P>(
          xynet_error_instance::make_error_code(xynet_error::eof),
          m_bytes_transferred);
      }
      return async_throw_or_return<P>(async_operation_base::get_error_code(), m_bytes_transferred);
    }

  private:
    F& m_socket;
    BufferSequence m_buffers;
    int m_bytes_transferred;
    ::msghdr m_msghdr;
  };


  template<typename... Args>
  [[nodiscard]]
  decltype(auto) recv_all(Args&&... args)
  noexcept (detail::FileDescriptorPolicyUseErrorCode<P>)
  {
    return async_recvmsg<decltype(buffer_sequence{std::forward<Args>(args)...})>{*static_cast<F*>(this), std::forward<Args>(args)...};
  }

};

}

#endif //XYNET_READ_H
