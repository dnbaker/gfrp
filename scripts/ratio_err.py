import sys
import numpy as np
from subprocess import check_call

def load_data(path):
    with open(path) as f:
        return np.array([list(map(float, i[:-1].split(", "))) for i in f if i[0] != "#" and "1e-300" not in i], dtype=np.double)


def print_results(path, sigma, n, ofp):
    print("loading data")
    data = load_data(path)
    print("loaded data")
    # data[:,2] *= sigma * sigma
    mean_fac = np.mean(data[:,1] / data[:,2])
    ofp.write("%f\t%f\t%f\t%f\t%f\t%i\n" % (np.mean(data[:,1]), np.mean(data[:,2]), mean_fac, sigma, np.corrcoef(data[:,1], data[:,2])[0,1], n))
    print("mf: %f. %f, %f\n" % (mean_fac,  np.mean(data[:,1]), np.mean(data[:,2])))
    ratios = data[:,1] / data[:,2]
    print("ratmean %f, ratstd %f" % (np.mean(ratios), np.std(ratios)))
    ratios = data[:,1] / data[:,2]
    print("n: %i. corr: %e. ratio off %e. size %e" % (
        len(data), np.corrcoef(data[:,1], data[:,2])[0,1], mean_fac, len(data[:,1])))
    inv =  -np.reciprocal(data[:,2])
    inv[inv > 1e30] = 1e-300
    print("neginv n: %i. corr: %e. ratio off %e. size %e" % (
        len(data), np.corrcoef(data[:,1], inv)[0,1], mean_fac, len(data[:,1])))
    tmp = np.subtract(data[:,2], 1)
    print("corr of 1 - el and original is %lf" % np.corrcoef(tmp, data[:,1])[0,1])
    tablefp = open("table.txt", "w")
    for el, invel, fullk in zip(data[:,2], inv, data[:,1]):
        print("el %lf\tInv el %lf\tfullk el %lf" % (el, invel, fullk), file=tablefp)


def submit_work(tup):
    sig, SIZE, fn = tup[:]
    s  = "kernel_test -i256 -s %f -S %i  > %s" % (sig, SIZE, fn)
    check_call(s, shell=True)
    return fn


def get_data():
    import multiprocessing
    SIGS = [0.01, 0.05, 0.1, 1, 10, 100, 10000]
    SIZE = 1 << 16
    spool = multiprocessing.Pool(8)
    fns = spool.map(submit_work,
                    [[sig, SIZE, "output.%s.txt" % (sig)] for sig in SIGS])
    return ["output.%s.txt" % (sig) for sig in SIGS]


def print_ratios_and_corrs(path, sig):
    data = load_data(path)
    d = data
    correlations = np.array([np.corrcoef(data[:,1], data[:,i])[0,1] for i in range(1, 6)])
    ratios = np.array([np.mean(d[:,i] / d[:,1]) for i in range(1,6)])
    mratios = np.array([np.mean(d[:,i]) / np.mean(d[:,1]) for i in range(1,6)])
    stds = np.array([np.std(d[:,i] / d[:,1]) for i in range(1,6)])
    rmses = np.array([np.sqrt(sum((c - est)**2 for c, est in zip(d[:,i], d[:,1])) / len(d[:,i])) for i in range(1,6)])
    names = ["exact", "rff", "orff", "sorff", "ff"]
    for ind, (c, r, m, s, rmse) in enumerate(zip(correlations, ratios, mratios, stds, rmses)):
        print("\t".join([names[ind], str(c), str(r), str(s), str(np.mean(d[:,ind+1])), str(m), str(rmse), str(sig)]))


def main():
    SIGS = [i / 10. for i in range(2, 50)]
    print("Name\tCorrelation\tRatio\tRatio std\tMean\tRatio of means\tRMSE\tSigma")
    # SIGS = [0.01, 0.05, 0.1, 1, 10, 100, 10000]
    if sys.argv[1:]:
        fns = ["output.%s.txt" % (sig) for sig in SIGS]
        for fn, sig in zip(fns, SIGS):
            print_ratios_and_corrs(fn, sig)
        return
    import multiprocessing
    SIZE = 1 << 16
    spool = multiprocessing.Pool(8)
    fns = spool.map(submit_work,
                    [[sig, SIZE, "output.%s.txt" % (sig)] for sig in SIGS])
    for fn, sig in zip(fns, SIGS):
        print_ratios_and_corrs(fn, sig)

def old_main():
    import multiprocessing
    SIGS = [i / 10. for i in range(16)]
    SIZE = 1 << 16
    ratsigf = open("ratsig.%s.txt" % (SIZE), "w")
    ratsigf.write("#KernelMean\tApproxMean\tRatio\tSigma\tCorrcoef\tRMSE\tN\n")
    spool = multiprocessing.Pool(8)
    fns = spool.map(submit_work,
                    [[sig, SIZE, "output.%s.txt" % (sig)] for sig in SIGS])
    [print_results(fn, sig, SIZE, ratsigf) for sig, fn in zip(SIGS, fns)]

if __name__ == "__main__":
    main()
