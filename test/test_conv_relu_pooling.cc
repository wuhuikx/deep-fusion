#include "test_utils.h"

using namespace deepfusion;
using format = deepfusion::memory::format;

struct test_conv_relu_pool_params {
   memory::nchw_dims src_dims;
   memory::pair_dims conv_kernel;
   memory::pair_dims conv_pad;
   memory::pair_dims conv_stride;

   memory::pair_dims pool_kernel;
   memory::pair_dims pool_pad;
   memory::pair_dims pool_stride;
   memory::nchw_dims dst_dims;
};


template <typename data_t_src, typename data_t_wei,
         typename data_t_acc, typename data_t_dst>
class conv_relu_pooling_test : public ::testing::TestWithParam<test_conv_relu_pool_params> {

  void check_result(const test_conv_relu_pool_params& pm,
                    const std::unique_ptr<memory>& src,
                    const std::unique_ptr<memory>& wei,
                    const std::unique_ptr<memory>& bias,
                    const std::unique_ptr<memory>& dst){

      mkldnn::engine eng = mkldnn::engine(mkldnn::engine::cpu, 0);

      auto mkldnn_dt_src = testutils::to_mkldnn_dtype(src->data_type());
      auto mkldnn_dt_wei = testutils::to_mkldnn_dtype(wei->data_type());
      auto mkldnn_dt_dst = testutils::to_mkldnn_dtype(dst->data_type());
      
      auto mkldnn_fmt_src = mkldnn::memory::format::nhwc;
      auto mkldnn_fmt_wei = mkldnn::memory::format::OIhw4i16o4i;
      auto mkldnn_fmt_dst = mkldnn::memory::format::nhwc;
      auto mkldnn_fmt_bia = mkldnn::memory::format::x;

      // src memory preparation
      //mkldnn::memory mkldnn_src;
      //mkldnn::memory::primitive_desc src_pd;
      auto mkldnn_src_dims = testutils::to_mkldnn_dims(pm.src_dims);
      auto src_desc = mkldnn::memory::desc(mkldnn_src_dims, mkldnn_dt_src, mkldnn_fmt_src);
      auto src_pd = mkldnn::memory::primitive_desc(src_desc, eng);
      auto src_memory = mkldnn::memory(src_pd);
      utils::copy_array<data_t_src>((data_t_src*)(src_memory.get_data_handle()), 
                                    (data_t_src*)(src->data()), 
                                    src->size());

      // weight memory preparation
      auto mkldnn_wei_dims = testutils::to_mkldnn_dims({pm.dst_dims[3], pm.src_dims[3], 
              pm.conv_kernel[0], pm.conv_kernel[1]});
      auto wei_desc = mkldnn::memory::desc(mkldnn_wei_dims, mkldnn_dt_wei, mkldnn_fmt_wei);
      auto wei_pd = mkldnn::memory::primitive_desc(wei_desc, eng);
      auto wei_memory = mkldnn::memory(wei_pd);
      utils::copy_array<data_t_wei>((data_t_wei*)(wei_memory.get_data_handle()), 
                                    (data_t_wei*)(wei->data()), 
                                    wei->size());

      // bias memory preparation
      auto mkldnn_bias_dims = testutils::to_mkldnn_dims({pm.dst_dims[-1]});
      auto bias_desc = mkldnn::memory::desc(mkldnn_bias_dims, mkldnn_dt_dst, mkldnn_fmt_dst);
      auto bias_pd = mkldnn::memory::primitive_desc(bias_desc, eng);
      auto bias_memory = mkldnn::memory(bias_pd);
      utils::copy_array<data_t_dst>((data_t_dst*)(bias_memory.get_data_handle()), 
                                    (data_t_dst*)(bias->data()), 
                                    bias->size());

      // dst memory preparation
      auto mkldnn_dst_dims = testutils::to_mkldnn_dims(pm.dst_dims);
      auto dst_desc = mkldnn::memory::desc(mkldnn_dst_dims, mkldnn_dt_dst, mkldnn_fmt_dst);
      auto dst_pd = mkldnn::memory::primitive_desc(dst_desc, eng);
      auto dst_memory = mkldnn::memory(dst_pd);


      // conv_desc
      std::unique_ptr<mkldnn::convolution_forward::desc> convFwd_desc;
      convFwd_desc.reset(new mkldnn::convolution_forward::desc(
                  mkldnn::prop_kind::forward_scoring,
                  mkldnn::algorithm::convolution_direct, 
                  src_desc, wei_desc, bias_desc, dst_desc, 
                  {pm.conv_stride[0], pm.conv_stride[1]},
                  {pm.conv_pad[0], pm.conv_pad[1]},
                  {pm.conv_pad[0], pm.conv_pad[1]},
                  mkldnn::padding_kind::zero));

      
      // conv_primitive_desc
      mkldnn::primitive_attr attr;
      int mask = 0;
      int count = pm.dst_dims[3];
      std::vector<float> scales(count);
      attr.set_output_scales(mask, scales);
      attr.set_int_output_round_mode(mkldnn::round_mode::round_nearest);

      mkldnn::post_ops ops;
      float scale = 1.0f;
      float negative_slope = 0.0f;
      float alpha = negative_slope; //negative slope for mkldnn_eltwise_relu.
      float beta = 1.0f; //ignored for mkldnn_eltwise_relu.
      ops.append_eltwise(scale, mkldnn::algorithm::eltwise_relu, alpha, beta);
      attr.set_post_ops(ops);

      std::unique_ptr<mkldnn::convolution_forward::primitive_desc> convFwd_pd;
      convFwd_pd.reset(new mkldnn::convolution_forward::primitive_desc(*convFwd_desc, attr, eng));

      // conv_prmitive
      std::unique_ptr<mkldnn::convolution_forward> convFwd;
      convFwd.reset(new mkldnn::convolution_forward(*convFwd_pd, 
                  src_memory, wei_memory, bias_memory, dst_memory));



  }

protected:
    virtual void SetUp(){

        test_conv_relu_pool_params p = ::testing::TestWithParam<test_conv_relu_pool_params>::GetParam();
        auto dtype_src = utils::type2dtype<data_t_src>::dtype;
        auto dtype_wei = utils::type2dtype<data_t_wei>::dtype;
        auto dtype_acc = utils::type2dtype<data_t_acc>::dtype;
        auto dtype_dst = utils::type2dtype<data_t_dst>::dtype;
        std::cout<< "=========pooling test========"<<std::endl;
        
        std::unique_ptr<memory> src;
        src.reset(new memory(p.src_dims, format::nhwc, dtype_src));
        testutils::fill_data<data_t_src>(static_cast<data_t_src*>(src->data()), src->size());
      
        std::unique_ptr<memory> weight;
        weight.reset(new memory({p.dst_dims[3], p.src_dims[3], p.conv_kernel[0], p.conv_kernel[1]}, format::OIhw4i16o4i, dtype_wei));
        testutils::fill_data<data_t_wei>(static_cast<data_t_wei*>(weight->data()), weight->size());

        std::unique_ptr<memory> dst;
        dst.reset(new memory(p.dst_dims, format::nhwc, dtype_dst));

        // TODO:add conv_relu_fuse

        //check_result(p, src, weight, dst);

    }
};
/*
struct test_conv_relu_pool_prams {
   memory::nchw_dims src_dims;
   memory::dims conv_kh, conv_kw;
   memory::dims conv_padR, conv_padL;
   memory::dims conv_strh, conv_strw;

   memory::dims pool_kh, pool_kw;
   memory::dims pool_padR, pool_padL;
   memory::dims pool_strh, pool_strw;
   memory::nchw_dims dst_dims;
}*/

using pooling_test_s32 = conv_relu_pooling_test<u8, s8, s32, s32>;
TEST_P(pooling_test_s32, TestsPooling) {}
INSTANTIATE_TEST_CASE_P(
        TestConvReluPooling, pooling_test_s32, ::testing::Values(
          test_conv_relu_pool_params{
          {1, 16, 4, 4}, {3, 3}, {0, 0}, {1, 1}, {2, 2}, {0, 0}, {2, 2}, {1, 16, 1, 1}}
        )
);
