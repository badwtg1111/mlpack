
#include <fastlib/fastlib.h>
#include "affineNMF.h"

const fx_entry_doc anmf_entries[] = {
  {"i1", FX_PARAM, FX_STR, NULL,
   "  input file 1.\n"}, 
  {"i2", FX_PARAM, FX_STR, NULL,
   "  input file 2.\n"},
  {"i3", FX_PARAM, FX_STR, NULL,
   "  input file 3.\n"},
  {"input", FX_PARAM, FX_STR_LIST, NULL,
   "  input images (i1,i2).\n"},
  {"BInit", FX_PARAM, FX_STR_LIST, NULL,
   "  input bases (i4,i5).\n"},
  {"sigma", FX_PARAM, FX_DOUBLE, NULL,
   "  sigma (0.5).\n"},
  {"gamma", FX_PARAM, FX_DOUBLE, NULL,
   "  gamma (0.1).\n"},
  {"maxIter", FX_PARAM, FX_INT, NULL,
   "  maxIter (100).\n"},
  /*
  {"fileE", FX_REQUIRED, FX_STR, NULL,
   "  A file containing HMM emission.\n"},
  {"length", FX_PARAM, FX_INT, NULL,
   "  Sequence length, default = 10.\n"},
  {"lenmax", FX_PARAM, FX_INT, NULL,
   "  Maximum sequence length, default = length\n"},
  {"numseq", FX_PARAM, FX_INT, NULL,
   "  Number of sequance, default = 10.\n"},
  {"fileSEQ", FX_PARAM, FX_STR, NULL,
   "  Output file for the generated sequences.\n"},
  */
  //{"statefile", FX_PARAM, FX_STR, NULL,
  // "  Output file for the generated state sequences.\n"},
  FX_ENTRY_DOC_DONE
};

const fx_submodule_doc anmf_submodules[] = {
  FX_SUBMODULE_DOC_DONE
};

const fx_module_doc anmf_doc = {
  anmf_entries, anmf_submodules,
  "This is a program generating sequences from HMM models.\n"
};

/*
void InitRandom01(index_t n_rows, index_t n_cols, Matrix* A_) {
  Matrix& A = *A_;
  A.Init(n_rows, n_cols);
  for (index_t i = 0; i < n_rows; i++) 
    for (index_t j = 0; j < n_cols; j++)
      A.ref(i, j) = math::Random(0.1,1.0000);
}


void nmf_run(const Matrix& V, index_t rank,
	     Matrix* W_, Matrix* H_) {
  Matrix Winit, Hinit;
  InitRandom01(V.n_rows(), rank, &Winit);
  InitRandom01(rank, V.n_cols(), &Hinit);
  
  nmf(V, Winit, Hinit, 10, W_, H_);
}
*/

int main(int argc, char* argv[]) {
  fx_module* root = fx_init(argc, argv, &anmf_doc);

  size_t n_images = 0; const char * def_images [] = {"i1", "i2"};
  const char** f1 = fx_param_str_array(root, "input", &n_images, 2, def_images);
  size_t n_bases = 0; const char * def_bases [] = {"i4", "i5"};
  const char** f2 = fx_param_str_array(root, "BInit", &n_bases, 2, def_bases); 

  ArrayList<ImageType> X;
  LoadImageList(X, f1, n_images);

  ArrayList<ImageType> B;
  if (f2[0][0] != '\0')
    LoadImageList(B, f2, n_bases); // B.PushBackCopy(i2); B.PushBackCopy(i3);
  else {
    printf("RANDOM BASES\n");
    n_bases = 1;
    index_t n_points = 20;
    RandomImageList(B, n_bases, n_points);
  }

  ArrayList<Transformation> T;
  T.Init(); 
  for (index_t i = 0; i < X.size(); i++) {
    Transformation t;
    T.PushBackCopy(t);
  }

  ArrayList<Vector> W;
  W.Init();
  for (index_t i = 0; i < X.size(); i++) {
    Vector w; w.Init(n_bases); w.SetAll(1.0);
    W.PushBackCopy(w);
  }

  register_all(X, T, W, B);

  ArrayList<ImageType> XRecover;
  CalculateRecovery(T, W, B, XRecover);

  FILE* f = fopen("out", "w");

  Save(f, "B", B);
  Save(f, "T", T);
  Save(f, "W", W);
  Save(f, "X", X);
  Save(f, "XRecover", XRecover);

  fclose(f);
  
  /*
  ImageType i1(f1), i2(f2), i3(f3), i4, i5;
  
  ArrayList<ImageType> I;
  I.Init(); I.PushBackCopy(i2); I.PushBackCopy(i3);

  Vector wInit, wOut; 
  wInit.Init(2); wInit.SetZero();
  register_weights(i1, I, wInit, wOut);
  ot::Print(wOut);
  */

  /*
  ImageType i1(f1), i2(f2), i3, i4, i5;
  
  printf("D(i1,i2) = %f\n", i1.Difference(i2));
  
  i2.Scale(i3, 2.0);

  printf("D(i1,i3) = %f\n", i1.Difference(i3));

  i3.Save("i3");

  Transformation t; t.m[2] = 2; t.m[5] = 1;
  i2.Transform(i4, t, 1.0);

  printf("D(i1,i4) = %f\n", i1.Difference(i4));

  i4.Save("i4");

  Transformation tInit, tOut;
  register_transform(i4, i1, tInit, tOut);

  tOut.Print();

  i1.Transform(i5, tOut);
  i5.Save("i5");
  */

  /*
  // Test registration 
  Matrix I1, I2;
  data::Load(f1, &I1);
  data::Load(f2, &I2);
  Vector m;
  projective_register(I1, I2, &m);
  ot::Print(m);

  // Test nmf 
  Matrix V;

  V.Init(400,5);
  for (int i = 0; i < 5; i++) {
    Matrix X;
    char fn[100];
    sprintf(fn, "im%d", i+1);
    data::Load(fn, &X);
    for (int j = 0; j < 400; j++) {
      V.ref(j,i) = X.get(j%20, j/20)/255;
    }
  }

  //data::Load("V", &V);
  prepare_for_nmf(V);
  printf("size(V) = %d x %d", V.n_rows(), V.n_cols());
  Matrix W, H;
  nmf_run(V, 2, &W, &H);
  data::Save("basis", W);
  printf("size(W) = %d x %d", W.n_rows(), W.n_cols());
  data::Save("weight", H);
  printf("size(H) = %d x %d", H.n_rows(), H.n_cols());
  */

  fx_done(root);
  return 0;
}

