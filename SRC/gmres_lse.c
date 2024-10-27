#include <stdio.h>
#include <math.h>
#include "../include/mpls_util.h"
#include "../include/mpls.h"
#include "../include/lapack.h"
#include "../include/my_wtime.h"


int gmres_lse_left_scal(int n, int mo, int no, int po, double *Ao, int lda,
    double *Bo, int ldb, double *As, int ldas, double *Bs, int ldbs,
    double *T2s, double alpha, double *workssub,
    double *b, double *x, int restrt, int maxiter, int *num_iter,
    double tol, double *work, int lwork)
{
    // As and Bs store the GRQ factorization result of Ao and Bo.
    // no, mo, po is the dimension of Ao and Bo.
    // n = no+mo+po is the dimension of [I 0 Ao; 0 0 Bo; Ao^T Bo^T 0].
    double start, over, time[5];
    double *rtmp, *r, *V, *H, *cs, *sn, *e1, *s, *vcur, *vec_tmp, *addvec;
    double *w, *y, *worksub;
    double bnrm2, temp, para_scal, error;
    double one = 1.0, zero = 0.0, invalpha = 1.0/alpha;
    int incx = 1, ldh, m, i, tempint;

    for (int j = 0; j < 5; j++)
        time[j] = 0.0;
    *num_iter = 0;

    m = restrt;
    rtmp = work;
    r = rtmp + n;
    V = r + n;
    H = V + n*(m+1);
    cs = H + (m+1)*m;
    sn = cs + m;
    e1 = sn + m;
    s = e1 + n;
    y = s + m+1;
    vcur = y + m;
    vec_tmp = vcur + n;
    addvec = vec_tmp + n;
    w = addvec + n;
    worksub = w + n;

    ldh = m+1;

    tempint = mo*no;
    temp = pow(dnrm2_(&tempint, Ao, &incx), 2);
    tempint = po*no;
    temp = temp + pow(dnrm2_(&tempint, Bo, &incx), 2);
    temp = sqrt(2.0*temp + (double)mo)*dnrm2_(&n, x, &incx);
    temp = temp + dnrm2_(&n, b, &incx);

    start = tic();
    dcopy_(&n, b, &incx, r, &incx);
    funcAxres_scal(n, mo, no, po, Ao, lda, Bo, ldb, alpha, x, r);//rtmp-A*x
    over = tic();
    time[1] = over-start+time[1];
    start = tic();
    funcpreleft_lse_scal(mo, no, po, As, ldas, Bs, ldbs, T2s, alpha,
        workssub, r, worksub);
    over = tic();
    time[2] = over-start+time[2];

    bnrm2 = dnrm2_(&n, r, &incx);
    if (bnrm2 == 0.0)
        bnrm2 = 1.0;

    error = dnrm2_(&n, r, &incx)/bnrm2;
    if (error < tol) return 0;

    bnrm2 = temp;

    e1[0] = 1.0;

    for (int iter = 0; iter < maxiter; iter++)
    {
        start = tic();
        dcopy_(&n, b, &incx, r, &incx);
        funcAxres_scal(n, mo, no, po, Ao, lda, Bo, ldb, alpha, x, r);//rtmp-A*x
        over = tic();
        time[1] = over-start+time[1];
        start = tic();
        funcpreleft_lse_scal(mo, no, po, As, ldas, Bs, ldbs, T2s, alpha,
            workssub, r, worksub);
        temp = dnrm2_(&n, r, &incx);
        para_scal = 1.0/temp;
        dcopy_(&n, r, &incx, V, &incx);
        dscal_(&n, &para_scal, V, &incx);
        dcopy_(&n, e1, &incx, s, &incx);
        dscal_(&n, &temp, s, &incx);
        over = tic();
        time[2] = over-start+time[2];
        for (i = 0; i < m; i++)
        {
            start = tic();
            *num_iter = *num_iter + 1;
            funcAx_scal(n, mo, no, po, Ao, lda, Bo, ldb, alpha, &V[i*n],
            w);//A*x
            over = tic();
            time[1] = over-start+time[1];
            start = tic();
            funcpreleft_lse_scal(mo, no, po, As, ldas, Bs, ldbs, T2s,
            alpha, workssub, w, worksub);
            over = tic();
            time[2] = over-start+time[2];
            start = tic();

            for (int k = 0; k <= i; k++)
            {
                H[i*ldh+k] = ddot_(&n, w, &incx, &V[k*n], &incx);
                para_scal = -H[i*ldh+k];
                daxpy_(&n, &para_scal, &V[k*n], &incx, w, &incx);
            }
            H[i*ldh+i+1] = dnrm2_(&n, w, &incx);
            para_scal = 1.0/H[i*ldh+i+1];
            dcopy_(&n, w, &incx, &V[(i+1)*n], &incx);
            dscal_(&n, &para_scal, &V[(i+1)*n], &incx);

            for (int k = 0; k <= i-1; k++)
            {
                temp = cs[k]*H[i*ldh+k] + sn[k]*H[i*ldh+k+1];
                H[i*ldh+k+1] = -sn[k]*H[i*ldh+k] + cs[k]*H[i*ldh+k+1];
                H[i*ldh+k] = temp;
            }
            rot_givens(H[i*ldh+i], H[i*ldh+i+1], &cs[i], &sn[i]);
            temp = cs[i]*s[i];
            s[i+1] = -sn[i]*s[i];
            s[i] = temp;
            H[i*ldh+i] = cs[i]*H[i*ldh+i] + sn[i]*H[i*ldh+i+1];
            H[i*ldh+i+1] = 0.0;
            over = tic();
            time[3] = over-start+time[3];

            error = fabs(s[i+1])/bnrm2;
            if (error <= tol)
            {
                i = i+1;
                dcopy_(&i, s, &incx, y, &incx);
                dtrsv_("U", "N", "N", &i, H, &ldh, y, &incx, 1, 1, 1);
                dgemv_("N", &n, &i, &one, V, &n, y, &incx, &zero,
                        vec_tmp, &incx, 1);
                daxpy_(&n, &one, vec_tmp, &incx, x, &incx);
                break;
            }

        }

        if (error <= tol) break;
        dcopy_(&m, s, &incx, y, &incx);
        dtrsv_("U", "N", "N", &m, H, &ldh, y, &incx, 1, 1, 1);
        dgemv_("N", &n, &m, &one, V, &n, y, &incx, &zero, vec_tmp,
                &incx, 1);
        daxpy_(&n, &one, vec_tmp, &incx, x, &incx);

        dcopy_(&n, b, &incx, r, &incx);
        funcAxres_scal(n, mo, no, po, Ao, lda, Bo, ldb, alpha, x, r);//rtmp-A*x
        funcpreleft_lse_scal(mo, no, po, As, ldas, Bs, ldbs, T2s, alpha,
        workssub, r, worksub);
        s[i+1] = dnrm2_(&n, r, &incx);
        error = s[i+1]/bnrm2;
        if (error <= tol) break;
    }

    if (error > tol)
    {
        printf("%% Not converge! Maxiter is too small!");
        return 1;
    }
    dprintmat("time_gmres", 5, 1, time, 5);
    return 0;
}
