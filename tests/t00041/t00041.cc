namespace clanguml::t00041 {

struct B {
};

struct A {
};

class AA : public A {
};

struct R {
};

struct RR;

struct D {
    RR *rr;
};

struct E {
};

struct F {
};

struct RR : public R {
    E *e;
    F *f;
};

struct RRR : public RR {
};

namespace ns1 {
struct N {
};

struct NN : public N {
};

struct NM : public N {
};
}

} // namespace clanguml::t00041
