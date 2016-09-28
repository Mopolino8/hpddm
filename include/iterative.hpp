 /*
   This file is part of HPDDM.

   Author(s): Pierre Jolivet <pierre.jolivet@enseeiht.fr>
        Date: 2014-11-05

   Copyright (C) 2011-2014 Université de Grenoble
                 2015      Eidgenössische Technische Hochschule Zürich
                 2016-     Centre National de la Recherche Scientifique

   HPDDM is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published
   by the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   HPDDM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with HPDDM.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _HPDDM_ITERATIVE_
#define _HPDDM_ITERATIVE_

namespace HPDDM {
template<class K>
struct EmptyOperator : OptionsPrefix {
    const underlying_type<K>* getScaling() const { return nullptr; }
    const int _n;
    EmptyOperator(int n) : OptionsPrefix(), _n(n) { }
    int getDof() const { return _n; }
    template<bool = true> bool start(const K* const, K* const, const unsigned short& = 1) const { return false; }
    void end(const bool) const { }
};
template<class Operator, class K>
struct CustomOperator : EmptyOperator<K> {
    const Operator* const _A;
    CustomOperator(const Operator* const A, int n) : EmptyOperator<K>(n), _A(A) { }
    void GMV(const K* const in, K* const out, const int& mu = 1) const;
};
template<class K>
struct CustomOperator<MatrixCSR<K>, K> : EmptyOperator<K> {
    const MatrixCSR<K>* const _A;
    CustomOperator(const MatrixCSR<K>* const A) : EmptyOperator<K>(A ? A->_n : 0), _A(A) { }
    void GMV(const K* const in, K* const out, const int& mu = 1) const {
        HPDDM::Wrapper<K>::csrmm(_A->_sym, &(EmptyOperator<K>::_n), &mu, _A->_a, _A->_ia, _A->_ja, in, out);
    }
};

/* Class: Iterative method
 *  A class that implements various iterative methods. */
class IterativeMethod {
    private:
        /* Function: outputResidual
         *  Prints information about the residual at a given iteration. */
        template<char T, class K>
        static void checkConvergence(const char verbosity, const unsigned short j, const unsigned short i, const underlying_type<K>& tol, const int& mu, const underlying_type<K>* const norm, const K* const res, short* const conv, const short sentinel) {
            for(unsigned short nu = 0; nu < mu; ++nu)
                if(conv[nu] == -sentinel && ((tol > 0.0 && std::abs(res[nu]) / norm[nu] <= tol) || (tol < 0.0 && std::abs(res[nu]) <= -tol)))
                    conv[nu] = i;
            if(verbosity > 2) {
                constexpr auto method = (T == 2 ? "CG" : (T == 4 ? "GCRODR" : "GMRES"));
                unsigned short tmp[2] { 0, 0 };
                underlying_type<K> beta = std::abs(res[0]);
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    if(conv[nu] != -sentinel)
                        ++tmp[0];
                    else if(std::abs(res[nu]) > beta) {
                        beta = std::abs(res[nu]);
                        tmp[1] = nu;
                    }
                }
                if(tol > 0.0)
                    std::cout << method << ": " << std::setw(3) << j << " " << beta << " " << norm[tmp[1]] << " " << beta / norm[tmp[1]] << " < " << tol;
                else
                    std::cout << method << ": " << std::setw(3) << j << " " << beta << " < " << -tol;
                if(mu > 1) {
                    std::cout << " (rhs #" << tmp[1] + 1;
                    if(tmp[0])
                        std::cout << ", " << tmp[0] << " converged rhs";
                    std::cout << ")";
                }
                std::cout << std::endl;
            }
        }
        template<char T, class K>
        static unsigned short checkBlockConvergence(const char verbosity, const int& i, const underlying_type<K>& tol, const int& mu, const int& d, const underlying_type<K>* const norm, const K* const res, const int ldh, K* const work, const unsigned short t) {
            underlying_type<K>* pt = reinterpret_cast<underlying_type<K>*>(work);
            unsigned short conv = 0;
            if(T == 3) {
                for(unsigned short nu = 0; nu < mu / t; ++nu) {
                    pt[nu] = std::sqrt(std::real(res[nu]));
                    if(((tol > 0.0 && pt[nu] / norm[nu] <= tol) || (tol < 0.0 && pt[nu] <= -tol)))
                        conv += t;
                }
            }
            else if(t <= 1)
                for(unsigned short nu = 0; nu < d; ++nu) {
                    int dim = nu + 1;
                    pt[nu] = Blas<K>::nrm2(&dim, res + nu * ldh, &i__1);
                    if(((tol > 0.0 && pt[nu] / norm[nu] <= tol) || (tol < 0.0 && pt[nu] <= -tol)))
                        ++conv;
                }
            else {
                std::fill_n(work, d, K());
                for(unsigned short nu = 0; nu < t; ++nu) {
                    int dim = nu + 1;
                    Blas<K>::axpy(&dim, &(Wrapper<K>::d__1), res + nu * ldh, &i__1, work, &i__1);
                }
                *pt = Blas<K>::nrm2(&d, work, &i__1);
                if(((tol > 0.0 && *pt / *norm <= tol) || (tol < 0.0 && *pt <= -tol)))
                    conv += t;
            }
            if(verbosity > 2) {
                constexpr auto method = (T == 3 ? "BCG" : (T == 5 ? "BGCRODR" : "BGMRES"));
                underlying_type<K>* max = std::max_element(pt, pt + d / t);
                if(tol > 0.0)
                    std::cout << method << ": " << std::setw(3) << i << " " << *max << " " <<  norm[std::distance(pt, max)] << " " <<  *max / norm[std::distance(pt, max)] << " < " << tol;
                else
                    std::cout << method << ": " << std::setw(3) << i << " " << *max << " < " << -tol;
                if(d != t) {
                    std::cout << " (rhs #" << std::distance(pt, max) + 1;
                    if(conv)
                        std::cout << ", " << conv / t << " converged rhs";
                    if(d != mu)
                        std::cout << ", " << mu - d << " deflated rhs";
                    std::cout << ")";
                }
                std::cout <<  std::endl;
            }
            return conv;
        }
        template<char T>
        static void convergence(const char verbosity, const unsigned short i, const unsigned short m) {
            if(verbosity) {
                constexpr auto method = (T == 1 ? "BGMRES" : (T == 2 ? "CG" : (T == 3 ? "BCG" : (T == 4 ? "GCRODR" : (T == 5 ? "BGCRODR" : (T == 6 ? "PCG" : "GMRES"))))));
                if(i != m + 1)
                    std::cout << method << " converges after " << i << " iteration" << (i > 1 ? "s" : "") << std::endl;
                else
                    std::cout << method << " does not converges after " << m << " iteration" << (m > 1 ? "s" : "") << std::endl;
            }
        }
        template<char T, class K>
        static void options(const std::string& prefix, K* const d, int* const i, unsigned short* const m, char* const id) {
            const Option& opt = *Option::get();
            d[0] = opt.val(prefix + "tol", 1.0e-6);
            m[0] = std::min(opt.val<short>(prefix + "max_it", 100), std::numeric_limits<short>::max());
            id[0] = opt.val<char>(prefix + "verbosity", 0);
            if(T == 1 || T == 5) {
                d[1] = opt.val(prefix + "initial_deflation_tol", -1.0);
                m[2] = opt.val<unsigned short>(prefix + "enlarge_krylov_subspace", 1);
            }
            if(T == 0 || T == 1 || T == 4 || T == 5)
                m[1] = std::min(static_cast<unsigned short>(std::numeric_limits<short>::max()), std::min(opt.val<unsigned short>(prefix + "gmres_restart", 40), m[0]));
            if(T == 0 || T == 1 || T == 2 || T == 4 || T == 5)
                id[1] = opt.val<char>(prefix + "variant", 1);
            if(T == 0 || T == 3)
                id[1 + (T == 0)] = opt.val<char>(prefix + (T == 0 ? "orthogonalization" : "qr"), 0);
            if(T == 1 || T == 4 || T == 5)
                id[2] = opt.val<char>(prefix + "orthogonalization", 0) + 4 * opt.val<char>(prefix + "qr", 0);
            if(T == 4 || T == 5) {
                *i = std::min(m[1] - 1, opt.val<int>(prefix + "recycle", 0));
                id[3] = opt.val<char>(prefix + "recycle_target", 0);
                id[4] = opt.val<char>(prefix + "recycle_strategy", 0) + 4 * (std::min(opt.val<unsigned short>(prefix + "recycle_same_system"), static_cast<unsigned short>(2)));
            }
            if(std::abs(d[0]) < std::numeric_limits<underlying_type<K>>::epsilon()) {
                if(id[0])
                    std::cout << "WARNING -- the tolerance of the iterative method was set to " << d[0]
#if __cpp_rtti || defined(__GXX_RTTI) || defined(__INTEL_RTTI__) || defined(_CPPRTTI)
                     << " which is lower than the machine epsilon for type " << demangle(typeid(underlying_type<K>).name())
#endif
                     << ", forcing the tolerance to " << 4 * std::numeric_limits<underlying_type<K>>::epsilon() << std::endl;
                d[0] = 4 * std::numeric_limits<underlying_type<K>>::epsilon();
            }
        }
        /* Function: allocate
         *  Allocates workspace arrays for <Iterative method::CG>. */
        template<class K, typename std::enable_if<!Wrapper<K>::is_complex>::type* = nullptr>
        static void allocate(K*& dir, K*& p, const int& n, const unsigned short extra = 0, const unsigned short it = 1, const unsigned short mu = 1) {
            if(extra == 0) {
                dir = new K[(3 + std::max(1, 4 * n)) * mu];
                p = dir + 3 * mu;
            }
            else {
                dir = new K[(2 + 2 * it + std::max(1, (4 + extra * it) * n)) * mu];
                p = dir + (2 + 2 * it) * mu;
            }
        }
        template<class K, typename std::enable_if<Wrapper<K>::is_complex>::type* = nullptr>
        static void allocate(underlying_type<K>*& dir, K*& p, const int& n, const unsigned short extra = 0, const unsigned short it = 1, const unsigned short mu = 1) {
            if(extra == 0) {
                dir = new underlying_type<K>[3 * mu];
                p = new K[std::max(1, 4 * n) * mu];
            }
            else {
                dir = new underlying_type<K>[(2 + 2 * it) * mu];
                p = new K[std::max(1, (4 + extra * it) * n) * mu];
            }
        }
        /* Function: updateSol
         *
         *  Updates a solution vector after convergence of <Iterative method::GMRES>.
         *
         * Template Parameters:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise.
         *    K              - Scalar type.
         *
         * Parameters:
         *    variant        - Type of preconditioning.
         *    n              - Size of the vector.
         *    x              - Solution vector.
         *    k              - Dimension of the Hessenberg matrix.
         *    h              - Hessenberg matrix.
         *    s              - Coefficients in the Krylov subspace.
         *    v              - Basis of the Krylov subspace. */
        template<bool excluded, class Operator, class K, class T>
        static void updateSol(const Operator& A, const char variant, const int& n, K* const x, const K* const* const h, K* const s, T* const* const v, const short* const hasConverged, const int& mu, K* const work, const int& deflated = -1) {
            static_assert(std::is_same<K, typename std::remove_const<T>::type>::value, "Wrong types");
            if(!excluded)
                computeMin(h, s, hasConverged, mu, deflated);
            addSol<excluded>(A, variant, n, x, std::distance(h[0], h[1]) / std::abs(deflated), s, v, hasConverged, mu, work, deflated);
        }
        template<class K>
        static void computeMin(const K* const* const h, K* const s, const short* const hasConverged, const int& mu, const int& deflated = -1, const int& shift = 0) {
            int ldh = std::distance(h[0], h[1]) / std::abs(deflated);
            if(deflated != -1) {
                int dim = std::abs(*hasConverged) - deflated * shift;
                int info;
                Lapack<K>::trtrs("U", "N", "N", &dim, &deflated, *h + deflated * shift * (1 + ldh), &ldh, s, &ldh, &info);
            }
            else
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    int dim = std::abs(hasConverged[nu]);
                    if(dim)
                        Blas<K>::trsv("U", "N", "N", &(dim -= shift), *h + shift * (1 + ldh) + (ldh / mu) * nu, &ldh, s + nu, &mu);
                }
        }
        template<bool excluded, class Operator, class K, class T>
        static void addSol(const Operator& A, const char variant, const int& n, K* const x, const int& ldh, const K* const s, T* const* const v, const short* const hasConverged, const int& mu, K* const work, const int& deflated = -1) {
            static_assert(std::is_same<K, typename std::remove_const<T>::type>::value, "Wrong types");
            K* const correction = (variant == 1 ? (std::is_const<T>::value ? (work + mu * n) : const_cast<K*>(v[ldh / (deflated == -1 ? mu : deflated) - 1])) : work);
            if(excluded || !n) {
                if(variant == 1)
                    A.template apply<excluded>(work, correction, deflated == -1 ? mu : deflated);
            }
            else {
                if(deflated == -1) {
                    int ldv = mu * n;
                    if(!variant) {
                        for(unsigned short nu = 0; nu < mu; ++nu)
                            if(hasConverged[nu]) {
                                int dim = std::abs(hasConverged[nu]);
                                Blas<K>::gemv("N", &n, &dim, &(Wrapper<K>::d__1), *v + nu * n, &ldv, s + nu, &mu, &(Wrapper<K>::d__1), x + nu * n, &i__1);
                            }
                    }
                    else {
                        for(unsigned short nu = 0; nu < mu; ++nu) {
                            int dim = std::abs(hasConverged[nu]);
                            Blas<K>::gemv("N", &n, &dim, &(Wrapper<K>::d__1), *v + nu * n, &ldv, s + nu, &mu, &(Wrapper<K>::d__0), work + nu * n, &i__1);
                        }
                        if(variant == 1)
                            A.template apply<excluded>(work, correction, mu);
                        for(unsigned short nu = 0; nu < mu; ++nu)
                            if(hasConverged[nu])
                                Blas<K>::axpy(&n, &(Wrapper<K>::d__1), correction + nu * n, &i__1, x + nu * n, &i__1);
                    }
                }
                else {
                    int dim = *hasConverged;
                    if(deflated == mu) {
                        if(!variant)
                            Blas<K>::gemm("N", "N", &n, &mu, &dim, &(Wrapper<K>::d__1), *v, &n, s, &ldh, &(Wrapper<K>::d__1), x, &n);
                        else {
                            Blas<K>::gemm("N", "N", &n, &mu, &dim, &(Wrapper<K>::d__1), *v, &n, s, &ldh, &(Wrapper<K>::d__0), work, &n);
                            if(variant == 1)
                                A.template apply<excluded>(work, correction, mu);
                            Blas<K>::axpy(&(dim = mu * n), &(Wrapper<K>::d__1), correction, &i__1, x, &i__1);
                        }
                    }
                    else {
                        Blas<K>::gemm("N", "N", &n, &deflated, &dim, &(Wrapper<K>::d__1), *v, &n, s, &ldh, &(Wrapper<K>::d__0), work, &n);
                        if(variant == 1)
                            A.template apply<excluded>(work, correction, deflated);
                        Blas<K>::gemm("N", "N", &n, &(dim = mu - deflated), &deflated, &(Wrapper<K>::d__1), correction, &n, s + deflated * ldh, &ldh, &(Wrapper<K>::d__1), x + deflated * n, &n);
                        Blas<K>::axpy(&(dim = deflated * n), &(Wrapper<K>::d__1), correction, &i__1, x, &i__1);
                    }
                }
            }
        }
        template<bool excluded, class Operator, class K, class T>
        static void updateSolRecycling(const Operator& A, const char variant, const int& n, K* const x, const K* const* const h, K* const s, K* const* const v, T* const norm, const K* const C, const K* const U, const short* const hasConverged, const int shift, const int mu, K* const work, const MPI_Comm& comm, const int& deflated = -1) {
            const Option& opt = *Option::get();
            const int ldh = std::distance(h[0], h[1]) / std::abs(deflated);
            const int dim = ldh / (deflated == -1 ? mu : deflated);
            if(C && U) {
                computeMin(h, s + shift * (deflated == -1 ? mu : deflated), hasConverged, mu, deflated, shift);
                const int ldv = (deflated == -1 ? mu : deflated) * n;
                if(deflated == -1) {
                    if(opt.val<unsigned short>(A.prefix("recycle_same_system")))
                        std::fill_n(s, shift * mu, K());
                    else {
                        if(!excluded && n)
                            for(unsigned short nu = 0; nu < mu; ++nu) {
                                if(std::abs(hasConverged[nu])) {
                                    K alpha = norm[nu];
                                    Blas<K>::gemv(&(Wrapper<K>::transc), &n, &shift, &alpha, C + nu * n, &ldv, v[shift] + nu * n , &i__1, &(Wrapper<K>::d__0), s + nu, &mu);
                                }
                            }
                        else
                            std::fill_n(s, shift * mu, K());
                        MPI_Allreduce(MPI_IN_PLACE, s, shift * mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                    }
                    if(!excluded && n)
                        for(unsigned short nu = 0; nu < mu; ++nu) {
                            if(std::abs(hasConverged[nu])) {
                                int diff = std::abs(hasConverged[nu]) - shift;
                                Blas<K>::gemv("N", &shift, &diff, &(Wrapper<K>::d__2), h[shift] + nu * dim, &ldh, s + shift * mu + nu, &mu, &(Wrapper<K>::d__1), s + nu, &mu);
                            }
                        }
                }
                else {
                    int bK = deflated * shift;
                    K beta;
                    if(opt.val<unsigned short>(A.prefix("recycle_same_system")))
                        beta = K();
                    else {
                        if(!excluded && n) {
                            std::copy_n(v[shift], deflated * n, work);
                            Blas<K>::trmm("R", "U", "N", "N", &n, &deflated, &(Wrapper<K>::d__1), reinterpret_cast<K*>(norm), &ldh, work, &n);
                            Blas<K>::gemm(&(Wrapper<K>::transc), "N", &bK, &deflated, &n, &(Wrapper<K>::d__1), C, &n, work, &n, &(Wrapper<K>::d__0), s, &ldh);
                            for(unsigned short i = 0; i < deflated; ++i)
                                std::copy_n(s + i * ldh, bK, work + i * bK);
                        }
                        else
                            std::fill_n(work, bK * deflated, K());
                        MPI_Allreduce(MPI_IN_PLACE, work, bK * deflated, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                        for(unsigned short i = 0; i < deflated; ++i)
                            std::copy_n(work + i * bK, bK, s + i * ldh);
                        beta = Wrapper<K>::d__1;
                    }
                    int diff = *hasConverged - deflated * shift;
                    Blas<K>::gemm("N", "N", &bK, &deflated, &diff, &(Wrapper<K>::d__2), h[shift], &ldh, s + shift * deflated, &ldh, &beta, s, &ldh);
                }
                std::copy_n(U, shift * ldv, v[dim * (variant == 2)]);
                addSol<excluded>(A, variant, n, x, ldh, s, static_cast<const K* const* const>(v + dim * (variant == 2)), hasConverged, mu, work, deflated);
            }
            else
                updateSol<excluded>(A, variant, n, x, h, s, static_cast<const K* const* const>(v + dim * (variant == 2)), hasConverged, mu, work, deflated);
        }
        template<class T, typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr>
        static void clean(T* const& pt) {
            delete [] *pt;
            delete []  pt;
        }
        template<class T, typename std::enable_if<!std::is_pointer<T>::value>::type* = nullptr>
        static void clean(T* const& pt) {
            delete [] pt;
        }
        template<class K, class T, typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr>
        static void axpy(const int* const n, const K* const a, const T* const x, const int* const incx, T* const y, const int* const incy) {
            static_assert(std::is_same<typename std::remove_pointer<T>::type, K>::value, "Wrong types");
            Blas<typename std::remove_pointer<T>::type>::axpy(n, a, *x, incx, *y, incy);
        }
        template<class K, class T, typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr>
        static void axpy(const int* const, const K* const, const T* const, const int* const, T const, const int* const) { }
        template<class K, class T, typename std::enable_if<!std::is_pointer<T>::value>::type* = nullptr>
        static void axpy(const int* const n, const K* const a, const T* const x, const int* const incx, T* const y, const int* const incy) {
            static_assert(std::is_same<T, K>::value, "Wrong types");
            Blas<T>::axpy(n, a, x, incx, y, incy);
        }
        template<class T, typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr>
        static typename std::remove_pointer<T>::type dot(const int* const n, const T* const x, const int* const incx, const T* const y, const int* const incy) {
            return Blas<typename std::remove_pointer<T>::type>::dot(n, *x, incx, *y, incy) / 2.0;
        }
        template<class T, typename std::enable_if<!std::is_pointer<T>::value>::type* = nullptr>
        static T dot(const int* const n, const T* const x, const int* const incx, const T* const y, const int* const incy) {
            return Blas<T>::dot(n, x, incx, y, incy);
        }
        template<class T, class U, typename std::enable_if<std::is_pointer<T>::value>::type* = nullptr>
        static void diag(const int&, const U* const* const, T* const, T* const = nullptr) { }
        template<class T, typename std::enable_if<!std::is_pointer<T>::value>::type* = nullptr>
        static void diag(const int& n, const underlying_type<T>* const d, T* const in, T* const out = nullptr) {
            if(out)
                Wrapper<T>::diag(n, d, in, out);
            else
                Wrapper<T>::diag(n, d, in);
        }
        template<bool excluded, class Operator, class K>
        static bool initializeNorm(const Operator& A, const char variant, const K* const b, K* const x, K* const v, const int n, K* work, underlying_type<K>* const norm, const unsigned short mu, const unsigned short k) {
            bool allocate = A.template start<excluded>(b, x, mu);
            if(!variant) {
                A.template apply<excluded>(b, v, mu, work);
                if(k <= 1)
                    for(unsigned short nu = 0; nu < mu; ++nu)
                        norm[nu] = std::real(Blas<K>::dot(&n, v + nu * n, &i__1, v + nu * n, &i__1));
                else {
                    std::fill_n(work, n, K());
                    for(unsigned short nu = 0; nu < mu; ++nu)
                        Blas<K>::axpy(&n, &(Wrapper<K>::d__1), v + nu * n, &i__1, work, &i__1);
                    *norm = std::real(Blas<K>::dot(&n, work, &i__1, work, &i__1));
                }
            }
            else {
                if(k <= 1)
                    work = const_cast<K*>(b);
                else {
                    std::fill_n(work, n, K());
                    for(unsigned short nu = 0; nu < k; ++nu)
                        Blas<K>::axpy(&n, &(Wrapper<K>::d__1), b + nu * n, &i__1, work, &i__1);
                }
                for(unsigned short nu = 0; nu < mu / k; ++nu) {
                    norm[nu] = 0.0;
                    for(unsigned int i = 0; i < n; ++i) {
                        if(std::abs(work[nu * n + i]) > HPDDM_PEN * HPDDM_EPS)
                            norm[nu] += std::norm(work[nu * n + i] / underlying_type<K>(HPDDM_PEN));
                        else
                            norm[nu] += std::norm(work[nu * n + i]);
                    }
                }
            }
            return allocate;
        }
        /* Function: orthogonalization
         *
         *  Orthogonalizes a block of vectors against a contiguous set of block of vectors.
         *
         * Template Parameters:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise.
         *    K              - Scalar type.
         *
         * Parameters:
         *    id             - Type of orthogonalization procedure.
         *    n              - Size of the vectors to orthogonalize.
         *    k              - Size of the basis to orthogonalize against.
         *    mu             - Number of vectors in each block.
         *    B              - Pointer to the basis.
         *    v              - Input block of vectors.
         *    H              - Dot products.
         *    comm           - Global MPI communicator. */
        template<bool excluded, class K>
        static void orthogonalization(const char id, const int n, const int k, const int mu, const K* const B, K* const v, K* const H, const MPI_Comm& comm, const underlying_type<K>* const d = nullptr, K* const scal = nullptr) {
            if(excluded || !n) {
                std::fill_n(H, k * mu, K());
                if(id == 1)
                    for(unsigned short i = 0; i < k; ++i)
                        MPI_Allreduce(MPI_IN_PLACE, H + i * mu, mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                else
                    MPI_Allreduce(MPI_IN_PLACE, H, k * mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
            }
            else {
                if(id == 1)
                    for(unsigned short i = 0; i < k; ++i) {
                        K* const pt = d ? scal : v;
                        if(d)
                            Wrapper<K>::diag(n, d, v, scal, mu);
                        for(unsigned short nu = 0; nu < mu; ++nu)
                            H[i * mu + nu] = Blas<K>::dot(&n, B + (i * mu + nu) * n, &i__1, pt + nu * n, &i__1);
                        MPI_Allreduce(MPI_IN_PLACE, H + i * mu, mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                        for(unsigned short nu = 0; nu < mu; ++nu) {
                            K alpha = -H[i * mu + nu];
                            Blas<K>::axpy(&n, &alpha, B + (i * mu + nu) * n, &i__1, v + nu * n, &i__1);
                        }
                    }
                else {
                    int ldb = mu * n;
                    K* const pt = d ? scal : v;
                    if(d)
                        Wrapper<K>::diag(n, d, v, scal, mu);
                    for(unsigned short nu = 0; nu < mu; ++nu)
                        Blas<K>::gemv(&(Wrapper<K>::transc), &n, &k, &(Wrapper<K>::d__1), B + nu * n, &ldb, pt + nu * n, &i__1, &(Wrapper<K>::d__0), H + nu, &mu);
                    MPI_Allreduce(MPI_IN_PLACE, H, k * mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                    for(unsigned short nu = 0; nu < mu; ++nu)
                        Blas<K>::gemv("N", &n, &k, &(Wrapper<K>::d__2), B + nu * n, &ldb, H + nu, &mu, &(Wrapper<K>::d__1), v + nu * n, &i__1);
                }
            }
        }
        template<bool excluded, class K>
        static void blockOrthogonalization(const char id, const int n, const int k, const int mu, const K* const B, K* const v, K* const H, const int ldh, K* const work, const MPI_Comm& comm) {
            if(excluded || !n) {
                std::fill_n(work, mu * mu * k, K());
                if(id == 1)
                    for(unsigned short i = 0; i < k; ++i) {
                        MPI_Allreduce(MPI_IN_PLACE, work, mu * mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                        Wrapper<K>::template omatcopy<'N'>(mu, mu, work, mu, H + mu * i, ldh);
                    }
                else {
                    MPI_Allreduce(MPI_IN_PLACE, work, mu * mu * k, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                    Wrapper<K>::template omatcopy<'N'>(mu, mu * k, work, mu * k, H, ldh);
                }
            }
            else {
                if(id == 1) {
                    for(unsigned short i = 0; i < k; ++i) {
                        Blas<K>::gemm(&(Wrapper<K>::transc), "N", &mu, &mu, &n, &(Wrapper<K>::d__1), B + i * mu * n, &n, v, &n, &(Wrapper<K>::d__0), work, &mu);
                        MPI_Allreduce(MPI_IN_PLACE, work, mu * mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                        Blas<K>::gemm("N", "N", &n, &mu, &mu, &(Wrapper<K>::d__2), B + i * mu * n, &n, work, &mu, &(Wrapper<K>::d__1), v, &n);
                        Wrapper<K>::template omatcopy<'N'>(mu, mu, work, mu, H + mu * i, ldh);
                    }
                }
                else {
                    int tmp = mu * k;
                    Blas<K>::gemm(&(Wrapper<K>::transc), "N", &tmp, &mu, &n, &(Wrapper<K>::d__1), B, &n, v, &n, &(Wrapper<K>::d__0), work, &tmp);
                    MPI_Allreduce(MPI_IN_PLACE, work, mu * tmp, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                    Blas<K>::gemm("N", "N", &n, &mu, &tmp, &(Wrapper<K>::d__2), B, &n, work, &tmp, &(Wrapper<K>::d__1), v, &n);
                    Wrapper<K>::template omatcopy<'N'>(mu, tmp, work, tmp, H, ldh);
                }
            }
        }
        /* Function: VR
         *  Computes the inverse of the upper triangular matrix of a QR decomposition using the Cholesky QR method. */
        template<bool excluded, class K>
        static void VR(const int n, const int k, const int mu, const K* const V, K* const R, const int ldr, const MPI_Comm& comm, K* work = nullptr, const underlying_type<K>* const d = nullptr, K* const scal = nullptr) {
            const int ldv = mu * n;
            if(!work)
                work = R;
            if(!excluded && n)
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    if(!d)
                        Blas<K>::herk("U", "C", &k, &n, &(Wrapper<underlying_type<K>>::d__1), V + nu * n, &ldv, &(Wrapper<underlying_type<K>>::d__0), work + nu * (k * (k + 1)) / 2, &k);
                    else {
                        if(mu == 1)
                            Wrapper<K>::diag(n, d, V, scal, k);
                        else {
                            for(unsigned short xi = 0; xi < k; ++xi)
                                Wrapper<K>::diag(n, d, V + nu * n + xi * ldv, scal + xi * n);
                        }
                        Blas<K>::gemmt("U", &(Wrapper<K>::transc), "N", &k, &n, &(Wrapper<K>::d__1), V + nu * n, &ldv, scal, &n, &(Wrapper<K>::d__0), work + nu * (k * (k + 1)) / 2, &k);
                    }
                    for(unsigned short xi = 1; xi < k; ++xi)
                        std::copy_n(work + nu * (k * (k + 1)) / 2 + xi * k, xi + 1, work + nu * (k * (k + 1)) / 2 + (xi * (xi + 1)) / 2);
                }
            else
                std::fill_n(work, mu * (k * (k + 1)) / 2, K());
            MPI_Allreduce(MPI_IN_PLACE, work, mu * (k * (k + 1)) / 2, Wrapper<K>::mpi_type(), MPI_SUM, comm);
            for(unsigned short nu = mu; nu-- > 0; )
                for(unsigned short xi = k; xi > 0; --xi)
                    std::copy_backward(work + nu * (k * (k + 1)) / 2 + (xi * (xi - 1)) / 2, work + nu * (k * (k + 1)) / 2 + (xi * (xi + 1)) / 2, R + nu * k * k + xi * ldr - (ldr - xi));
        }
        /* Function: QR
         *  Computes a QR decomposition of a distributed matrix. */
        template<bool excluded, class K>
        static int QR(const char id, const int n, const int k, const int mu, K* const Q, K* const R, const int ldr, const MPI_Comm& comm, K* work = nullptr, bool update = true, const underlying_type<K>* const d = nullptr, K* const scal = nullptr) {
            const int ldv = mu * n;
            if(id == 0) {
                VR<excluded>(n, k, mu, Q, R, ldr, comm, work, d, scal);
                int info;
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    Lapack<K>::potrf("U", &k, R + nu * k * k, &ldr, &info);
                    if(info > 0)
                        return info;
                }
                if(!excluded && n && update)
                    for(unsigned short nu = 0; nu < mu; ++nu)
                        Blas<K>::trsm("R", "U", "N", "N", &n, &k, &(Wrapper<K>::d__1), R + k * k * nu, &ldr, Q + nu * n, &ldv);
            }
            else {
                if(!work)
                    work = R;
                K* pt = d ? scal : Q;
                for(unsigned short xi = 0; xi < k; ++xi) {
                    if(xi > 0)
                        orthogonalization<excluded>(id - 1, n, xi, mu, Q, Q + xi * ldv, work + xi * k * mu, comm, d, scal);
                    if(d)
                        Wrapper<K>::diag(n, d, Q + xi * ldv, scal, mu);
                    for(unsigned short nu = 0; nu < mu; ++nu)
                        work[xi * (k + 1) * mu + nu] = Blas<K>::dot(&n, Q + xi * ldv + nu * n, &i__1, pt + nu * n, &i__1);
                    if(!d)
                        pt += ldv;
                    MPI_Allreduce(MPI_IN_PLACE, work + xi * (k + 1) * mu, mu, Wrapper<K>::mpi_type(), MPI_SUM, comm);
                    for(unsigned short nu = 0; nu < mu; ++nu) {
                        work[xi * (k + 1) * mu + nu] = std::sqrt(work[xi * (k + 1) * mu + nu]);
                        if(std::real(work[xi * (k + 1) * mu + nu]) < HPDDM_EPS)
                            return 1;
                        K alpha = K(1.0) / work[xi * (k + 1) * mu + nu];
                        Blas<K>::scal(&n, &alpha, Q + xi * ldv + nu * n, &i__1);
                    }
                }
                if(work != R)
                    Wrapper<K>::template omatcopy<'N'>(k, k * mu, work, k * mu, R, ldr);
            }
            return 0;
        }
        /* Function: Arnoldi
         *  Computes one iteration of the Arnoldi method for generating one basis vector of a Krylov space. */
        template<bool excluded, class K>
        static void Arnoldi(const char id, const unsigned short m, K* const* const H, K* const* const v, K* const s, underlying_type<K>* const sn, const int n, const int i, const int mu, const MPI_Comm& comm, K* const* const save = nullptr, const unsigned short shift = 0) {
            orthogonalization<excluded>(id % 4, n, i + 1 - shift, mu, v[shift], v[i + 1], H[i] + shift * mu, comm);
            for(unsigned short nu = 0; nu < mu; ++nu)
                sn[i * mu + nu] = excluded ? 0.0 : std::real(Blas<K>::dot(&n, v[i + 1] + nu * n, &i__1, v[i + 1] + nu * n, &i__1));
            MPI_Allreduce(MPI_IN_PLACE, sn + i * mu, mu, Wrapper<K>::mpi_underlying_type(), MPI_SUM, comm);
            for(unsigned short nu = 0; nu < mu; ++nu) {
                H[i][(i + 1) * mu + nu] = std::sqrt(sn[i * mu + nu]);
                if(!excluded && i < m - 1)
                    std::for_each(v[i + 1] + nu * n, v[i + 1] + (nu + 1) * n, [&](K& y) { y /= H[i][(i + 1) * mu + nu]; });
            }
            if(save)
                Wrapper<K>::template omatcopy<'T'>(i - shift + 2, mu, H[i] + shift * mu, mu, save[i - shift], m + 1);
            for(unsigned short k = shift; k < i; ++k) {
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    K gamma = Wrapper<K>::conj(H[k][(m + 1) * nu + k + 1]) * H[i][k * mu + nu] + sn[k * mu + nu] * H[i][(k + 1) * mu + nu];
                    H[i][(k + 1) * mu + nu] = -sn[k * mu + nu] * H[i][k * mu + nu] + H[k][(m + 1) * nu + k + 1] * H[i][(k + 1) * mu + nu];
                    H[i][k * mu + nu] = gamma;
                }
            }
            for(unsigned short nu = 0; nu < mu; ++nu) {
                const int tmp = 2;
                underlying_type<K> delta = Blas<K>::nrm2(&tmp, H[i] + i * mu + nu, &mu);
                sn[i * mu + nu] = std::real(H[i][(i + 1) * mu + nu]) / delta;
                H[i][(i + 1) * mu + nu] = H[i][i * mu + nu] / delta;
                H[i][i * mu + nu] = delta;
                s[(i + 1) * mu + nu] = -sn[i * mu + nu] * s[i * mu + nu];
                s[i * mu + nu] *= Wrapper<K>::conj(H[i][(i + 1) * mu + nu]);
            }
            if(mu > 1)
                Wrapper<K>::template imatcopy<'T'>(i + 2, mu, H[i], mu, m + 1);
        }
        /* Function: BlockArnoldi
         *  Computes one iteration of the Block Arnoldi method for generating one basis vector of a block Krylov space. */
        template<bool excluded, class K>
        static bool BlockArnoldi(const char id, const unsigned short m, K* const* const H, K* const* const v, K* const tau, K* const s, const int lwork, const int n, const int i, const int mu, K* const work, const MPI_Comm& comm, K* const* const save = nullptr, const unsigned short shift = 0) {
            int ldh = (m + 1) * mu;
            blockOrthogonalization<excluded>(id % 4, n, i + 1 - shift, mu, v[shift], v[i + 1], H[i] + shift * mu, ldh, work, comm);
            int info = QR<excluded>(id / 4, n, mu, 1, v[i + 1], H[i] + (i + 1) * mu, ldh, comm, work, i < m - 1);
            if(info > 0)
                return true;
            for(unsigned short nu = 0; nu < mu; ++nu)
                std::fill(H[i] + (i + 1) * mu + nu * ldh + nu + 1, H[i] + (nu + 1) * ldh, K());
            if(save)
                for(unsigned short nu = 0; nu < mu; ++nu)
                    std::copy_n(H[i] + shift * mu + nu * ldh, (i + 1 - shift) * mu + nu + 1, save[i - shift] + nu * ldh);
            int N = 2 * mu;
            for(unsigned short leading = shift; leading < i; ++leading)
                Lapack<K>::mqr("L", &(Wrapper<K>::transc), &N, &mu, &N, H[leading] + leading * mu, &ldh, tau + leading * N, H[i] + leading * mu, &ldh, work, &lwork, &info);
            Lapack<K>::geqrf(&N, &mu, H[i] + i * mu, &ldh, tau + i * N, work, &lwork, &info);
            Lapack<K>::mqr("L", &(Wrapper<K>::transc), &N, &mu, &N, H[i] + i * mu, &ldh, tau + i * N, s + i * mu, &ldh, work, &lwork, &info);
            return false;
        }
        template<class Operator, class K, typename std::enable_if<hpddm_method_id<Operator>::value>::type* = nullptr>
        static void preprocess(const Operator&, const K* const, K*&, K* const, K*&, const int&, unsigned short&, const MPI_Comm&);
        template<class Operator, class K, typename std::enable_if<hpddm_method_id<Operator>::value>::type* = nullptr>
        static void postprocess(const Operator&, const K* const, K*&, K* const, K*&, const int&, unsigned short&);
        template<class Operator, class K, typename std::enable_if<!hpddm_method_id<Operator>::value>::type* = nullptr>
        static void preprocess(const Operator& A, const K* const b, K*& sb, K* const x, K*& sx, const int& mu, unsigned short& k, const MPI_Comm& comm) {
            int size;
            MPI_Comm_size(comm, &size);
            const std::string prefix = A.prefix();
            if(k < 2 || size == 1) {
                sx = x;
                sb = const_cast<K*>(b);
                Option::get()->remove(prefix + "enlarge_krylov_subspace");
            }
            else {
                int rank;
                MPI_Comm_rank(comm, &rank);
                k = std::min(k, static_cast<unsigned short>(size));
                const int n = A.getDof();
                sb = new K[k * mu * n]();
                sx = new K[k * mu * n]();
                const unsigned short j = std::min(k - 1, rank / (size / k));
                if(j >= k)
                    std::cout << "PANIC" << std::endl;
                for(unsigned short nu = 0; nu < mu; ++nu) {
                    std::copy_n(x + nu * n, n, sx + (j + k * nu) * n);
                    std::copy_n(b + nu * n, n, sb + (j + k * nu) * n);
                }
                Option& opt = *Option::get();
                opt[prefix + "enlarge_krylov_subspace"] = k;
                if(mu > 1)
                    opt.remove(prefix + "initial_deflation_tol");
                if(!opt.any_of(prefix + "krylov_method", { 1, 3, 5 })) {
                    opt[prefix + "krylov_method"] = 1;
                    if(opt.val<char>(prefix + "verbosity", 0))
                        std::cout << "WARNING -- block iterative methods should be used when enlarging Krylov subspaces, now switching to BGMRES" << std::endl;
                }
            }
        }
        template<class Operator, class K, typename std::enable_if<!hpddm_method_id<Operator>::value>::type* = nullptr>
        static void postprocess(const Operator& A, const K* const b, K*& sb, K* const x, K*& sx, const int& mu, unsigned short& k) {
            if(sb != b) {
                const int n = A.getDof();
                std::fill_n(x, mu * n, K());
                for(unsigned short nu = 0; nu < mu; ++nu)
                    for(unsigned short j = 0; j < k; ++j)
                        Blas<K>::axpy(&n, &(Wrapper<K>::d__1), sx + (j + k * nu) * n, &i__1, x + nu * n, &i__1);
                delete [] sx;
                delete [] sb;
            }
        }
    public:
        /* Function: GMRES
         *
         *  Implements the GMRES.
         *
         * Template Parameters:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise.
         *    K              - Scalar type.
         *
         * Parameters:
         *    A              - Global operator.
         *    b              - Right-hand side(s).
         *    x              - Solution vector(s).
         *    mu             - Number of right-hand sides.
         *    comm           - Global MPI communicator. */
        template<bool, class Operator, class K>
        static int GMRES(const Operator& A, const K* const b, K* const x, const int& mu, const MPI_Comm& comm);
        template<bool, class Operator, class K>
        static int BGMRES(const Operator&, const K* const, K* const, const int&, const MPI_Comm&);
        template<bool, class Operator, class K>
        static int GCRODR(const Operator&, const K* const, K* const, const int&, const MPI_Comm&);
        template<bool, class Operator, class K>
        static int BGCRODR(const Operator&, const K* const, K* const, const int&, const MPI_Comm&);
        /* Function: CG
         *
         *  Implements the CG method.
         *
         * Template Parameters:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise.
         *    K              - Scalar type.
         *
         * Parameters:
         *    A              - Global operator.
         *    b              - Right-hand side.
         *    x              - Solution vector.
         *    comm           - Global MPI communicator. */
        template<bool, class Operator, class K>
        static int CG(const Operator& A, const K* const b, K* const x, const int&, const MPI_Comm& comm);
        template<bool, class Operator, class K>
        static int BCG(const Operator& A, const K* const b, K* const x, const int&, const MPI_Comm& comm);
        /* Function: PCG
         *
         *  Implements the projected CG method.
         *
         * Template Parameters:
         *    excluded       - True if the master processes are excluded from the domain decomposition, false otherwise.
         *    K              - Scalar type.
         *
         * Parameters:
         *    A              - Global operator.
         *    b              - Right-hand side.
         *    x              - Solution vector.
         *    comm           - Global MPI communicator. */
        template<bool excluded = false, class Operator, class K>
        static int PCG(const Operator& A, const K* const b, K* const x, const MPI_Comm& comm);
        template<bool excluded = false, class Operator = void, class K = double, typename std::enable_if<!is_substructuring_method<Operator>::value>::type* = nullptr>
        static int solve(const Operator& A, const K* const b, K* const x, const int& mu
#if HPDDM_MPI
                                                                        , const MPI_Comm& comm) {
#else
                                                                                              ) {
            int comm = 0;
#endif
            std::ios_base::fmtflags ff(std::cout.flags());
            std::cout << std::scientific;
            const std::string prefix = A.prefix();
            Option& opt = *Option::get();
#if HPDDM_MIXED_PRECISION
            opt[prefix + "variant"] = 2;
#endif
            unsigned short k = opt.val<unsigned short>(prefix + "enlarge_krylov_subspace", 1);
            K* sx = nullptr;
            K* sb = nullptr;
            preprocess(A, b, sb, x, sx, mu, k, comm);
            int it;
            switch(opt.val<char>(prefix + "krylov_method")) {
                case 5:  it = HPDDM::IterativeMethod::BGCRODR<excluded>(A, sb, sx, k * mu, comm); break;
                case 4:  it = HPDDM::IterativeMethod::GCRODR<excluded>(A, sb, sx, k * mu, comm); break;
                case 3:  it = HPDDM::IterativeMethod::BCG<excluded>(A, sb, sx, k * mu, comm); break;
                case 2:  it = HPDDM::IterativeMethod::CG<excluded>(A, sb, sx, k * mu, comm); break;
                case 1:  it = HPDDM::IterativeMethod::BGMRES<excluded>(A, sb, sx, k * mu, comm); break;
                default: it = HPDDM::IterativeMethod::GMRES<excluded>(A, sb, sx, k * mu, comm);
            }
            postprocess(A, b, sb, x, sx, mu, k);
            std::cout.flags(ff);
            return it;
        }
        template<bool excluded = false, class Operator = void, class K = double, typename std::enable_if<is_substructuring_method<Operator>::value>::type* = nullptr>
        static int solve(const Operator& A, const K* const b, K* const x, const int&, const MPI_Comm& comm) {
            return HPDDM::IterativeMethod::PCG<excluded>(A, b, x, comm);
        }
};
} // HPDDM
#endif // _HPDDM_ITERATIVE_
